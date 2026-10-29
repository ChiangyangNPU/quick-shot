#include "PetSystemMonitor.h"

#include <QTimer>

#if defined(Q_OS_MAC)
#include <mach/mach_host.h>
#include <mach/host_info.h>
#include <mach/vm_statistics.h>
#include <sys/sysctl.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace {
// 轮询间隔：5s。系统状态不是秒级需求，低频轮询避免自身成为性能负担
constexpr int kPollMs = 5000;
// CPU/内存过载阈值（占用 > 85% 视为过载）
constexpr double kOverloadThreshold = 0.85;
// 低电量阈值（电量 < 20% 且未充电时提醒）
constexpr int kLowBatteryPercent = 20;
}

/**
 * @brief 构造函数
 * @author chiangyang
 */
PetSystemMonitor::PetSystemMonitor(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kPollMs);
    connect(m_timer, &QTimer::timeout, this, &PetSystemMonitor::poll);
}

/**
 * @brief 启动轮询
 * @author chiangyang
 */
void PetSystemMonitor::start() {
    // CPU 占用靠两次快照差分，重启轮询后重新建立基线
    m_hasPrevCpu = false;
    poll();
    m_timer->start();
}

/**
 * @brief 停止轮询
 * @author chiangyang
 */
void PetSystemMonitor::stop() {
    m_timer->stop();
    if (m_alert != Alert::None) {
        m_alert = Alert::None;
        emit alertChanged(m_alert);
    }
}

/**
 * @brief 轮询：采样三项指标并按阈值计算告警状态
 * @author chiangyang
 */
void PetSystemMonitor::poll() {
    double cpu = 0.0;
    if (sampleCpu(cpu)) {
        m_cpu = cpu;
    }
    double mem = 0.0;
    if (sampleMemory(mem)) {
        m_memory = mem;
    }
    int pct = -1;
    bool charging = false;
    if (sampleBattery(pct, charging)) {
        m_battery = pct;
        m_charging = charging;
    } else {
        m_battery = -1;
        m_charging = false;
    }

    // 低电量优先于过载（且一次会话只提示一次，提示过后该条件不再参与告警）
    Alert next = Alert::None;
    if (m_battery >= 0 && m_battery < kLowBatteryPercent && !m_charging && !m_batteryNotified) {
        next = Alert::LowBattery;
    } else if (m_cpu > kOverloadThreshold || m_memory > kOverloadThreshold) {
        next = Alert::Overload;
    }
    if (next == Alert::LowBattery) {
        m_batteryNotified = true;
    }
    if (next != m_alert) {
        m_alert = next;
        emit alertChanged(m_alert);
    }
}

#if defined(Q_OS_MAC)

/**
 * @brief 采样 CPU 占用（macOS：host_statistics HOST_CPU_LOAD_INFO 差分）
 * @author chiangyang
 */
bool PetSystemMonitor::sampleCpu(double &usage) {
    host_cpu_load_info_data_t info;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        reinterpret_cast<host_info_t>(&info), &count) != KERN_SUCCESS) {
        return false;
    }
    const quint64 idle = info.cpu_ticks[CPU_STATE_IDLE];
    quint64 total = 0;
    for (int i = 0; i < CPU_STATE_MAX; ++i) {
        total += info.cpu_ticks[i];
    }
    if (!m_hasPrevCpu) {
        m_prevCpuIdle = idle;
        m_prevCpuTotal = total;
        m_hasPrevCpu = true;
        usage = 0.0;
        return true;
    }
    const quint64 dIdle = idle - m_prevCpuIdle;
    const quint64 dTotal = total - m_prevCpuTotal;
    m_prevCpuIdle = idle;
    m_prevCpuTotal = total;
    if (dTotal == 0) {
        return false;
    }
    usage = 1.0 - double(dIdle) / double(dTotal);
    return true;
}

/**
 * @brief 采样内存占用（macOS：活跃 + 联动 + 压缩页占物理内存比例）
 * @author chiangyang
 */
bool PetSystemMonitor::sampleMemory(double &usage) {
    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm), &count) != KERN_SUCCESS) {
        return false;
    }
    uint64_t totalBytes = 0;
    size_t len = sizeof(totalBytes);
    if (sysctlbyname("hw.memsize", &totalBytes, &len, nullptr, 0) != 0 || totalBytes == 0) {
        return false;
    }
    vm_size_t pageSize = 0;
    if (host_page_size(mach_host_self(), &pageSize) != KERN_SUCCESS || pageSize == 0) {
        return false;
    }
    const uint64_t used = (uint64_t(vm.active_count) + vm.wire_count
                           + vm.compressor_page_count) * pageSize;
    usage = double(used) / double(totalBytes);
    return true;
}

/**
 * @brief 采样电池状态（macOS：IOKit 电源信息，取内置电池）
 * @author chiangyang
 */
bool PetSystemMonitor::sampleBattery(int &percent, bool &charging) {
    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (!info) {
        return false;
    }
    CFArrayRef list = IOPSCopyPowerSourcesList(info);
    if (!list) {
        CFRelease(info);
        return false;
    }
    bool found = false;
    const CFIndex n = CFArrayGetCount(list);
    for (CFIndex i = 0; i < n; ++i) {
        CFTypeRef ps = CFArrayGetValueAtIndex(list, i);
        CFDictionaryRef desc = IOPSGetPowerSourceDescription(info, ps);
        if (!desc) {
            continue;
        }
        CFStringRef type = static_cast<CFStringRef>(
            CFDictionaryGetValue(desc, CFSTR(kIOPSTypeKey)));
        if (!type || CFStringCompare(type, CFSTR(kIOPSInternalBatteryType), 0) != kCFCompareEqualTo) {
            continue;
        }
        int cur = 0;
        int max = 0;
        CFNumberRef curRef = static_cast<CFNumberRef>(
            CFDictionaryGetValue(desc, CFSTR(kIOPSCurrentCapacityKey)));
        CFNumberRef maxRef = static_cast<CFNumberRef>(
            CFDictionaryGetValue(desc, CFSTR(kIOPSMaxCapacityKey)));
        if (curRef) {
            CFNumberGetValue(curRef, kCFNumberIntType, &cur);
        }
        if (maxRef) {
            CFNumberGetValue(maxRef, kCFNumberIntType, &max);
        }
        if (max <= 0) {
            continue;
        }
        percent = int(100.0 * cur / max + 0.5);
        CFBooleanRef chargingRef = static_cast<CFBooleanRef>(
            CFDictionaryGetValue(desc, CFSTR(kIOPSIsChargingKey)));
        charging = chargingRef && CFBooleanGetValue(chargingRef);
        found = true;
        break;
    }
    CFRelease(list);
    CFRelease(info);
    return found;
}

#elif defined(Q_OS_WIN)

namespace {
// FILETIME 转 64 位计数
quint64 fileTimeToUInt64(const FILETIME &ft) {
    return (quint64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}
}

/**
 * @brief 采样 CPU 占用（Windows：GetSystemTimes 差分，kernel 含 idle）
 * @author chiangyang
 */
bool PetSystemMonitor::sampleCpu(double &usage) {
    FILETIME idleT, kernelT, userT;
    if (!GetSystemTimes(&idleT, &kernelT, &userT)) {
        return false;
    }
    const quint64 idle = fileTimeToUInt64(idleT);
    const quint64 total = fileTimeToUInt64(kernelT) + fileTimeToUInt64(userT);
    if (!m_hasPrevCpu) {
        m_prevCpuIdle = idle;
        m_prevCpuTotal = total;
        m_hasPrevCpu = true;
        usage = 0.0;
        return true;
    }
    const quint64 dIdle = idle - m_prevCpuIdle;
    const quint64 dTotal = total - m_prevCpuTotal;
    m_prevCpuIdle = idle;
    m_prevCpuTotal = total;
    if (dTotal == 0) {
        return false;
    }
    usage = 1.0 - double(dIdle) / double(dTotal);
    return true;
}

/**
 * @brief 采样内存占用（Windows：GlobalMemoryStatusEx 直接给出负载百分比）
 * @author chiangyang
 */
bool PetSystemMonitor::sampleMemory(double &usage) {
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) {
        return false;
    }
    usage = double(ms.dwMemoryLoad) / 100.0;
    return true;
}

/**
 * @brief 采样电池状态（Windows：GetSystemPowerStatus）
 * @author chiangyang
 */
bool PetSystemMonitor::sampleBattery(int &percent, bool &charging) {
    SYSTEM_POWER_STATUS sps;
    if (!GetSystemPowerStatus(&sps)) {
        return false;
    }
    // BatteryFlag 高位 128 表示无电池；BatteryLifePercent 255 表示未知
    if ((sps.BatteryFlag & 128) || sps.BatteryLifePercent > 100) {
        return false;
    }
    percent = int(sps.BatteryLifePercent);
    charging = (sps.ACLineStatus == 1) || (sps.BatteryFlag & 8);
    return true;
}

#else

// 其他平台暂不支持采集：全部返回失败，不产生告警
bool PetSystemMonitor::sampleCpu(double &) { return false; }
bool PetSystemMonitor::sampleMemory(double &) { return false; }
bool PetSystemMonitor::sampleBattery(int &, bool &) { return false; }

#endif
