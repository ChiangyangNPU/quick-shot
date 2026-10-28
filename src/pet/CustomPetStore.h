#ifndef CUSTOMPETSTORE_H
#define CUSTOMPETSTORE_H

#include <QString>
#include <QVector>

/**
 * @brief 自定义桌面宠物素材记录
 *
 * 素材统一入库到应用数据目录下的 pets/ 子目录，配置只保存文件名，
 * 避免用户原始图片被移动或删除后宠物失效。
 * @author chiangyang
 */
struct CustomPet {
    QString id;          ///< 唯一标识（UUID，配置主键）
    QString name;        ///< 显示名称（默认取原文件名）
    QString fileName;    ///< 入库后的文件名（位于 pets 目录）
    bool isGif = false;  ///< 是否为 GIF 动图

    /**
     * @brief 记录是否有效
     * @return id 非空即为有效
     * @author chiangyang
     */
    bool isValid() const { return !id.isEmpty(); }
};

/**
 * @brief 自定义桌面宠物素材仓库（单例）
 *
 * 负责自定义宠物图片/GIF 的导入、删除与配置持久化：
 * - 素材目录：QStandardPaths::AppDataLocation/pets/
 * - 列表配置：pet/customPets（JSON 数组）
 * - 当前选择：pet/selection（"builtin:<枚举值>" 或 "custom:<id>"）
 * - 兼容旧配置：pet/selection 不存在时从旧的 pet/style 迁移
 * @author chiangyang
 */
class CustomPetStore {
public:
    /**
     * @brief 获取单例实例
     * @return CustomPetStore 实例指针
     * @author chiangyang
     */
    static CustomPetStore *instance();

    /**
     * @brief 获取已导入的自定义宠物列表
     * @return 记录列表（按导入顺序）
     * @author chiangyang
     */
    QVector<CustomPet> list() const;

    /**
     * @brief 按 id 查找自定义宠物
     * @param id 唯一标识
     * @return 找到返回记录，否则返回无效记录（isValid() 为 false）
     * @author chiangyang
     */
    CustomPet find(const QString &id) const;

    /**
     * @brief 获取记录的素材文件绝对路径
     * @param pet 记录
     * @return 素材文件路径
     * @author chiangyang
     */
    QString filePath(const CustomPet &pet) const;

    /**
     * @brief 导入一张自定义宠物素材
     *
     * 支持 PNG/JPG/JPEG/WebP/GIF；静态图最长边超过 1024 时等比缩小后
     * 统一存为 PNG（保留透明），GIF 直接复制（限 20MB）。
     * @param sourcePath 用户选择的原图路径
     * @param errorMessage 失败原因输出（可为 nullptr）
     * @return 导入成功返回记录，失败返回无效记录
     * @author chiangyang
     */
    CustomPet add(const QString &sourcePath, QString *errorMessage = nullptr);

    /**
     * @brief 删除自定义宠物（同时删除入库文件）
     * @param id 唯一标识
     * @return 是否删除成功
     * @author chiangyang
     */
    bool remove(const QString &id);

    /**
     * @brief 获取当前选择（"builtin:<n>" 或 "custom:<id>"）
     *
     * 首次调用时若只有旧的 pet/style，会自动迁移为 builtin 选择。
     * @return 选择标识
     * @author chiangyang
     */
    QString selection() const;

    /**
     * @brief 持久化当前选择
     * @param selection 选择标识
     * @author chiangyang
     */
    void setSelection(const QString &selection);

    /**
     * @brief 构造内置样式选择标识
     * @param style PetStyle 枚举值
     * @return "builtin:<style>"
     * @author chiangyang
     */
    static QString builtinSelection(int style);

    /**
     * @brief 构造自定义宠物选择标识
     * @param id 素材 id
     * @return "custom:<id>"
     * @author chiangyang
     */
    static QString customSelection(const QString &id);

    /**
     * @brief 判断选择标识是否指向自定义宠物
     * @param selection 选择标识
     * @return 是否为自定义选择
     * @author chiangyang
     */
    static bool isCustomSelection(const QString &selection);

    /**
     * @brief 从选择标识中取出自定义宠物 id
     * @param selection 选择标识
     * @return 自定义 id（非自定义选择时为空）
     * @author chiangyang
     */
    static QString customIdFromSelection(const QString &selection);

    /**
     * @brief 从选择标识中取出内置样式枚举值
     * @param selection 选择标识
     * @param fallback 无法解析时的回退值
     * @return PetStyle 枚举值
     * @author chiangyang
     */
    static int builtinStyleFromSelection(const QString &selection, int fallback = 0);

private:
    /**
     * @brief 构造函数（私有，单例模式）
     * @author chiangyang
     */
    CustomPetStore() = default;

    /**
     * @brief 素材目录绝对路径（不存在时自动创建）
     * @return pets 目录路径
     * @author chiangyang
     */
    QString petsDir() const;

    /**
     * @brief 读取并解析列表配置
     * @return 记录列表
     * @author chiangyang
     */
    QVector<CustomPet> loadList() const;

    /**
     * @brief 写回列表配置
     * @param pets 记录列表
     * @author chiangyang
     */
    void saveList(const QVector<CustomPet> &pets) const;

    static CustomPetStore *s_instance;  ///< 单例实例
};

#endif // CUSTOMPETSTORE_H
