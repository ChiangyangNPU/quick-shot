#ifndef SECRETSTORE_H
#define SECRETSTORE_H

#include <QString>
#include <QSettings>

/**
 * @brief 敏感凭据安全存储（翻译引擎 API Key 等）
 *
 * 三种平台后端：
 * - Windows：DPAPI（CryptProtectData，用户范围加密，无需交互）——密文 Blob
 *   经 Base64 存回 INI 的 secure/ 前缀键下，仍以 INI 为单一设置存储；
 * - macOS：Keychain 通用密码（service="QuickShot"，account=配置键）——
 *   凭据完全不落 INI；
 * - Linux/其他：无系统级标准凭据库，降级为 INI 明文（secure/ 前缀）并记
 *   一次警告（isEncrypted() 为 false，调用方可感知）。
 *
 * 迁移：readWithLegacyMigration() 在读到安全存储为空、而旧明文键仍有值时，
 * 自动搬入安全存储并删除明文键（老配置无损升级）。
 * 线程约束：主线程（经 ConfigManager 的 QSettings / 平台凭据 API）。
 * @author chiangyang
 */
class SecretStore {
public:
    enum class Backend { WindowsDPAPI, MacKeychain, PlainFallback };

    /**
     * @brief 当前平台后端
     * @author chiangyang
     */
    static Backend backend();

    /**
     * @brief 当前平台是否提供真加密存储（false = 明文降级）
     * @author chiangyang
     */
    static bool isEncrypted() { return backend() != Backend::PlainFallback; }

    /**
     * @brief 读取凭据；不存在或读取失败返回空串
     * @param key 配置键（如 "translate/baiduKey"）
     * @author chiangyang
     */
    static QString readSecret(const QString &key);

    /**
     * @brief 存储凭据；plain 为空串时等价于删除该凭据
     * @return 是否成功（失败时明文不应被删除，由调用方决定降级策略）
     * @author chiangyang
     */
    static bool storeSecret(const QString &key, const QString &plain);

    /**
     * @brief 删除凭据
     * @author chiangyang
     */
    static void removeSecret(const QString &key);

    /**
     * @brief 读取凭据并自动迁移旧明文配置
     *
     * 优先读安全存储；为空时回退读 settings 中的旧明文键，且在平台加密
     * 可用时将旧值搬入安全存储并从 settings 删除明文键（迁移失败保留明文
     * 并记警告，功能不受影响）。
     * @param settings 旧配置所在的 QSettings（通常为 ConfigManager::getSettings()）
     * @param key 配置键
     * @author chiangyang
     */
    static QString readWithLegacyMigration(QSettings *settings, const QString &key);
};

#endif // SECRETSTORE_H
