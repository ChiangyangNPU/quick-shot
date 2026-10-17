#include "SecretStore.h"
#include "ConfigManager.h"
#include "../log/Logger.h"

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

namespace {

constexpr char kSecurePrefix[] = "secure/";
#ifdef Q_OS_MACOS
constexpr char kKeychainService[] = "QuickShot";

CFStringRef cfString(const QByteArray &bytes) {
    return CFStringCreateWithBytes(kCFAllocatorDefault,
                                   reinterpret_cast<const UInt8 *>(bytes.constData()),
                                   bytes.size(), kCFStringEncodingUTF8, false);
}

// 构造 Keychain 查询字典：通用密码 + service + account（withData 时附带返回数据标记）
CFMutableDictionaryRef keychainQuery(const QString &key, bool withData) {
    CFStringRef service = cfString(kKeychainService);
    CFStringRef account = cfString(key.toUtf8());
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 4,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    if (withData)
        CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFRelease(service);
    CFRelease(account);
    return query;
}
#endif

QSettings *configSettings() {
    return ConfigManager::instance()->getSettings();
}

} // namespace

// ============================================================
// 后端判定
// ============================================================

SecretStore::Backend SecretStore::backend() {
#ifdef Q_OS_WIN
    return Backend::WindowsDPAPI;
#elif defined(Q_OS_MACOS)
    return Backend::MacKeychain;
#else
    return Backend::PlainFallback;
#endif
}

// ============================================================
// Windows：DPAPI（密文 Blob 经 Base64 存 INI 的 secure/ 键）
// ============================================================

#ifdef Q_OS_WIN

QString SecretStore::readSecret(const QString &key) {
    const QString b64 = configSettings()->value(QString(kSecurePrefix) + key).toString();
    if (b64.isEmpty()) {
        return QString();
    }

    const QByteArray blob = QByteArray::fromBase64(b64.toLatin1());
    DATA_BLOB in{}, out{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(blob.constData()));
    in.cbData = static_cast<DWORD>(blob.size());
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        LOG_ERROR(QString("SecretStore: CryptUnprotectData failed for '%1' (err %2)")
                      .arg(key).arg(GetLastError()));
        return QString();
    }
    const QString plain = QString::fromWCharArray(
        reinterpret_cast<const wchar_t *>(out.pbData),
        static_cast<int>(out.cbData / sizeof(wchar_t)));
    LocalFree(out.pbData);
    // 去掉加密时附带写入的尾部 NUL
    while (plain.endsWith(QChar(u'\0'))) plain.chop(1);
    return plain;
}

bool SecretStore::storeSecret(const QString &key, const QString &plain) {
    if (plain.isEmpty()) {
        removeSecret(key);
        return true;
    }

    const std::wstring w = plain.toStdWString();
    DATA_BLOB in{}, out{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<wchar_t *>(w.data()));
    in.cbData = static_cast<DWORD>((w.size() + 1) * sizeof(wchar_t));  // 含 NUL
    if (!CryptProtectData(&in, L"QuickShot", nullptr, nullptr, nullptr, 0, &out)) {
        LOG_ERROR(QString("SecretStore: CryptProtectData failed for '%1' (err %2)")
                      .arg(key).arg(GetLastError()));
        return false;
    }
    const QByteArray blob(reinterpret_cast<const char *>(out.pbData),
                          static_cast<int>(out.cbData));
    LocalFree(out.pbData);

    configSettings()->setValue(QString(kSecurePrefix) + key,
                               QString::fromLatin1(blob.toBase64()));
    return true;
}

void SecretStore::removeSecret(const QString &key) {
    configSettings()->remove(QString(kSecurePrefix) + key);
}

// ============================================================
// macOS：Keychain 通用密码（凭据不落 INI）
// ============================================================

#elif defined(Q_OS_MACOS)

QString SecretStore::readSecret(const QString &key) {
    CFMutableDictionaryRef query = keychainQuery(key, true);
    CFDataRef data = nullptr;
    const OSStatus status = SecItemCopyMatching(query, reinterpret_cast<CFTypeRef *>(&data));
    CFRelease(query);
    if (status != errSecSuccess || !data) {
        if (status != errSecItemNotFound) {
            LOG_WARNING(QString("SecretStore: SecItemCopyMatching failed for '%1' (status %2)")
                            .arg(key).arg(status));
        }
        return QString();
    }
    const QString plain = QString::fromUtf8(
        reinterpret_cast<const char *>(CFDataGetBytePtr(data)),
        static_cast<int>(CFDataGetLength(data)));
    CFRelease(data);
    return plain;
}

bool SecretStore::storeSecret(const QString &key, const QString &plain) {
    if (plain.isEmpty()) {
        removeSecret(key);
        return true;
    }

    const QByteArray bytes = plain.toUtf8();
    CFDataRef data = CFDataCreate(kCFAllocatorDefault,
                                  reinterpret_cast<const UInt8 *>(bytes.constData()),
                                  bytes.size());
    CFMutableDictionaryRef query = keychainQuery(key, false);

    // 优先原地更新（避免"先删旧再加新"在 Add 失败时丢失旧值）
    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kSecValueData, data);
    OSStatus status = SecItemUpdate(query, attrs);
    CFRelease(attrs);

    if (status == errSecItemNotFound) {
        CFDictionarySetValue(query, kSecValueData, data);
        status = SecItemAdd(query, nullptr);
    }
    CFRelease(data);
    CFRelease(query);
    if (status != errSecSuccess) {
        LOG_ERROR(QString("SecretStore: Keychain write failed for '%1' (status %2)")
                      .arg(key).arg(status));
        return false;
    }
    return true;
}

void SecretStore::removeSecret(const QString &key) {
    CFMutableDictionaryRef query = keychainQuery(key, false);
    SecItemDelete(query);  // 不存在时返回 errSecItemNotFound，无需处理
    CFRelease(query);
}

// ============================================================
// Linux/其他：明文降级（INI 的 secure/ 键）
// ============================================================

#else

QString SecretStore::readSecret(const QString &key) {
    return configSettings()->value(QString(kSecurePrefix) + key).toString();
}

bool SecretStore::storeSecret(const QString &key, const QString &plain) {
    if (plain.isEmpty()) {
        removeSecret(key);
        return true;
    }
    static bool warned = false;
    if (!warned) {
        LOG_WARNING("SecretStore: no encrypted storage on this platform, "
                    "secrets are stored in plain text");
        warned = true;
    }
    configSettings()->setValue(QString(kSecurePrefix) + key, plain);
    return true;
}

void SecretStore::removeSecret(const QString &key) {
    configSettings()->remove(QString(kSecurePrefix) + key);
}

#endif

// ============================================================
// 旧明文迁移
// ============================================================

QString SecretStore::readWithLegacyMigration(QSettings *settings, const QString &key) {
    QString value = readSecret(key);
    if (!value.isEmpty()) {
        return value;
    }

    const QString legacy = settings ? settings->value(key).toString() : QString();
    if (legacy.isEmpty()) {
        return QString();
    }

    // 平台加密可用：搬入安全存储并删除明文键（老配置无损升级）；
    // 迁移失败保留明文并记警告，功能不受影响
    if (isEncrypted()) {
        if (storeSecret(key, legacy)) {
            if (settings) settings->remove(key);
            LOG_INFO(QString("SecretStore: '%1' migrated to encrypted storage").arg(key));
        } else {
            LOG_WARNING(QString("SecretStore: '%1' migration failed, keeping plain text").arg(key));
        }
    }
    return legacy;
}
