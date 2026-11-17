#include "ScheduledTaskStore.h"
#include "../log/Logger.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace {

constexpr int kFileVersion = 1;                     ///< 数据文件版本号
constexpr char kFileName[] = "scheduled_tasks.json"; ///< 任务数据文件名

/**
 * @brief 校验任务时间是否为合法的 HH:mm
 * @param time 时间字符串
 * @return 是否合法
 * @author chiangyang
 */
bool isValidTaskTime(const QString &time) {
    return QTime::fromString(time, QStringLiteral("HH:mm")).isValid();
}

/**
 * @brief 掩码只保留低 7 位（周一至周日）
 * @param mask 原始掩码
 * @return 截断后的掩码
 * @author chiangyang
 */
int sanitizedMask(int mask) {
    return mask & 0x7F;
}

} // namespace

ScheduledTaskStore *ScheduledTaskStore::s_instance = nullptr;
QString ScheduledTaskStore::s_filePathOverride;

/**
 * @brief 获取单例实例
 * @author chiangyang
 */
ScheduledTaskStore *ScheduledTaskStore::instance() {
    if (!s_instance) {
        s_instance = new ScheduledTaskStore();
    }
    return s_instance;
}

/**
 * @brief 构造函数
 * @author chiangyang
 */
ScheduledTaskStore::ScheduledTaskStore(QObject *parent) : QObject(parent) {}

/**
 * @brief 数据文件绝对路径
 * @author chiangyang
 */
QString ScheduledTaskStore::filePath() const {
    if (!s_filePathOverride.isEmpty()) {
        return s_filePathOverride;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QString::fromLatin1(kFileName));
}

/**
 * @brief 从磁盘读取并解析任务列表
 * @author chiangyang
 */
QVector<ScheduledTask> ScheduledTaskStore::load(bool *ok) const {
    if (ok) *ok = false;
    QVector<ScheduledTask> tasks;
    QFile file(filePath());
    if (!file.exists()) {
        // 首次使用尚无数据文件，属于正常情况
        if (ok) *ok = true;
        return tasks;
    }
    if (!file.open(QFile::ReadOnly)) {
        // 文件存在但不可读（权限/占用等）：磁盘状态未知，ok 保持 false
        LOG_WARNING(QString("ScheduledTaskStore: cannot open file for reading: %1").arg(filePath()));
        return tasks;
    }

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        // JSON 损坏：数据可能仍可手工恢复，禁止后续覆盖写
        LOG_WARNING(QString("ScheduledTaskStore: invalid json, %1").arg(error.errorString()));
        return tasks;
    }

    const QJsonArray array = doc.object().value(QStringLiteral("tasks")).toArray();
    tasks.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        ScheduledTask task;
        task.id = obj.value(QStringLiteral("id")).toString();
        task.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
        task.days = sanitizedMask(obj.value(QStringLiteral("days")).toInt());
        task.time = obj.value(QStringLiteral("time")).toString();
        task.url = obj.value(QStringLiteral("url")).toString();
        // id 与合法时间是任务可调度的最低要求，残缺记录直接跳过
        if (!task.id.isEmpty() && isValidTaskTime(task.time)) {
            tasks.append(task);
        }
    }
    if (ok) *ok = true;
    return tasks;
}

/**
 * @brief 将任务列表写入磁盘
 * @author chiangyang
 */
bool ScheduledTaskStore::save(const QVector<ScheduledTask> &tasks) const {
    QJsonArray array;
    for (const ScheduledTask &task : tasks) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), task.id);
        obj.insert(QStringLiteral("enabled"), task.enabled);
        obj.insert(QStringLiteral("days"), task.days);
        obj.insert(QStringLiteral("time"), task.time);
        obj.insert(QStringLiteral("url"), task.url);
        array.append(obj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kFileVersion);
    root.insert(QStringLiteral("tasks"), array);

    // QSaveFile 先写临时文件再原子替换，异常退出也不会留下半截 JSON
    QSaveFile file(filePath());
    if (!file.open(QFile::WriteOnly)) {
        LOG_WARNING(QString("ScheduledTaskStore: cannot open file for writing: %1").arg(filePath()));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        LOG_WARNING(QString("ScheduledTaskStore: commit failed: %1").arg(filePath()));
        return false;
    }
    return true;
}

/**
 * @brief 获取全部任务
 * @author chiangyang
 */
QVector<ScheduledTask> ScheduledTaskStore::list() const {
    return load();
}

/**
 * @brief 新增一条任务
 * @author chiangyang
 */
ScheduledTask ScheduledTaskStore::add(const ScheduledTask &task) {
    if (!isValidTaskTime(task.time)) {
        LOG_WARNING(QString("ScheduledTaskStore: add rejected, invalid time: %1").arg(task.time));
        return ScheduledTask();
    }

    bool ok = false;
    QVector<ScheduledTask> tasks = load(&ok);
    if (!ok) {
        // 读失败时磁盘状态未知：基于空列表覆盖写回会清空全部存量任务，
        // 拒绝本次操作（返回无效项，UI 随后的重读不会显示它）
        LOG_ERROR(QString("ScheduledTaskStore: add aborted, disk state unknown (load failed)"));
        return ScheduledTask();
    }

    ScheduledTask created = task;
    created.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    created.days = sanitizedMask(created.days);
    created.url = normalizeUrl(created.url);
    tasks.append(created);

    if (!save(tasks)) {
        // 写盘失败（磁盘满/目录被删）必须显式留痕，否则静默丢数据
        LOG_ERROR(QString("ScheduledTaskStore: save failed, added task %1 may be lost").arg(created.id));
    }
    LOG_INFO(QString("ScheduledTaskStore: added task %1").arg(created.id));
    emit changed();
    return created;
}

/**
 * @brief 用新内容覆盖指定任务
 * @author chiangyang
 */
bool ScheduledTaskStore::update(const ScheduledTask &task) {
    if (task.id.isEmpty() || !isValidTaskTime(task.time)) {
        LOG_WARNING(QString("ScheduledTaskStore: update rejected, invalid task %1").arg(task.id));
        return false;
    }

    bool ok = false;
    QVector<ScheduledTask> tasks = load(&ok);
    if (!ok) {
        // 磁盘状态未知时拒绝写回（含全部存量），与 add() 同一防护
        LOG_ERROR(QString("ScheduledTaskStore: update aborted, disk state unknown (load failed)"));
        return false;
    }
    for (ScheduledTask &existing : tasks) {
        if (existing.id == task.id) {
            existing = task;
            existing.days = sanitizedMask(existing.days);
            existing.url = normalizeUrl(existing.url);
            if (!save(tasks)) {
                // 写盘失败（磁盘满/目录被删）必须显式留痕，否则静默丢数据
                LOG_ERROR(QString("ScheduledTaskStore: save failed, change to %1 may be lost").arg(task.id));
            }
            emit changed();
            return true;
        }
    }
    LOG_WARNING(QString("ScheduledTaskStore: update target not found: %1").arg(task.id));
    return false;
}

/**
 * @brief 删除指定任务
 * @author chiangyang
 */
bool ScheduledTaskStore::remove(const QString &id) {
    bool ok = false;
    QVector<ScheduledTask> tasks = load(&ok);
    if (!ok) {
        LOG_ERROR(QString("ScheduledTaskStore: remove aborted, disk state unknown (load failed)"));
        return false;
    }
    for (int i = 0; i < tasks.size(); ++i) {
        if (tasks.at(i).id == id) {
            tasks.removeAt(i);
            if (!save(tasks)) {
                LOG_ERROR(QString("ScheduledTaskStore: save failed, removal of %1 may be lost").arg(id));
            }
            LOG_INFO(QString("ScheduledTaskStore: removed task %1").arg(id));
            emit changed();
            return true;
        }
    }
    return false;
}

/**
 * @brief 判定星期掩码是否包含指定星期
 * @author chiangyang
 */
bool ScheduledTaskStore::matchesWeekday(int daysMask, int weekday) {
    return (daysMask & weekdayMask(weekday)) != 0;
}

/**
 * @brief 判定任务时间是否与给定时间处于同一分钟
 * @author chiangyang
 */
bool ScheduledTaskStore::matchesTime(const QString &taskTime, const QTime &now) {
    const QTime parsed = QTime::fromString(taskTime, QStringLiteral("HH:mm"));
    if (!parsed.isValid() || !now.isValid()) {
        return false;
    }
    return parsed.hour() == now.hour() && parsed.minute() == now.minute();
}

/**
 * @brief 星期转位掩码
 * @author chiangyang
 */
int ScheduledTaskStore::weekdayMask(int weekday) {
    if (weekday < 1 || weekday > 7) {
        return 0;
    }
    return 1 << (weekday - 1);
}

/**
 * @brief 规范化用户输入的网址
 * @author chiangyang
 */
QString ScheduledTaskStore::normalizeUrl(const QString &input) {
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    QUrl url(trimmed);
    if (url.scheme().isEmpty()) {
        // 用户通常只输入域名，统一补 https，避免落到 QUrl 默认的 http
        url = QUrl(QStringLiteral("https://") + trimmed);
    }
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty()
        || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
        return QString();
    }
    return url.toString();
}

/**
 * @brief 测试用：覆盖数据文件路径
 * @author chiangyang
 */
void ScheduledTaskStore::setFilePathForTest(const QString &path) {
    s_filePathOverride = path;
}
