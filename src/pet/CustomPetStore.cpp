#include "CustomPetStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

#include "ConfigManager.h"
#include "Logger.h"

namespace {

constexpr int kMaxStaticEdge = 1024;                 ///< 静态图入库最长边上限
constexpr int kMaxDecodeEdge = 8192;                 ///< 解码前文件头尺寸上限（超过直接拒绝）
constexpr int kMaxGifEdge = 1024;                    ///< GIF 帧最长边上限
constexpr qint64 kMaxGifBytes = 20 * 1024 * 1024;    ///< GIF 入库体积上限
constexpr char kPetsConfigKey[] = "pet/customPets";  ///< 自定义列表配置键
constexpr char kSelectionKey[] = "pet/selection";    ///< 当前选择配置键
constexpr char kLegacyStyleKey[] = "pet/style";      ///< 旧版内置样式配置键

} // namespace

CustomPetStore *CustomPetStore::s_instance = nullptr;

/**
 * @brief 获取单例实例
 * @author chiangyang
 */
CustomPetStore *CustomPetStore::instance() {
    if (!s_instance) {
        s_instance = new CustomPetStore();
    }
    return s_instance;
}

/**
 * @brief 构造内置样式选择标识
 * @author chiangyang
 */
QString CustomPetStore::builtinSelection(int style) {
    return QStringLiteral("builtin:%1").arg(style);
}

/**
 * @brief 构造自定义宠物选择标识
 * @author chiangyang
 */
QString CustomPetStore::customSelection(const QString &id) {
    return QStringLiteral("custom:%1").arg(id);
}

/**
 * @brief 判断选择标识是否指向自定义宠物
 * @author chiangyang
 */
bool CustomPetStore::isCustomSelection(const QString &selection) {
    return selection.startsWith(QStringLiteral("custom:"));
}

/**
 * @brief 从选择标识中取出自定义宠物 id
 * @author chiangyang
 */
QString CustomPetStore::customIdFromSelection(const QString &selection) {
    return isCustomSelection(selection) ? selection.mid(7) : QString();
}

/**
 * @brief 从选择标识中取出内置样式枚举值
 * @author chiangyang
 */
int CustomPetStore::builtinStyleFromSelection(const QString &selection, int fallback) {
    if (!selection.startsWith(QStringLiteral("builtin:"))) {
        return fallback;
    }
    bool ok = false;
    const int style = selection.mid(8).toInt(&ok);
    return ok ? style : fallback;
}

/**
 * @brief 素材目录绝对路径（不存在时自动创建）
 * @author chiangyang
 */
QString CustomPetStore::petsDir() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/pets");
    QDir().mkpath(dir);
    return dir;
}

/**
 * @brief 读取并解析列表配置
 * @author chiangyang
 */
QVector<CustomPet> CustomPetStore::loadList() const {
    QVector<CustomPet> pets;
    const QString json = ConfigManager::instance()->value(kPetsConfigKey).toString();
    if (json.isEmpty()) {
        return pets;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray()) {
        return pets;
    }
    const QJsonArray array = doc.array();
    pets.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        CustomPet pet;
        pet.id = obj.value(QStringLiteral("id")).toString();
        pet.name = obj.value(QStringLiteral("name")).toString();
        pet.fileName = obj.value(QStringLiteral("fileName")).toString();
        pet.isGif = obj.value(QStringLiteral("isGif")).toBool();
        if (!pet.id.isEmpty() && !pet.fileName.isEmpty()) {
            pets.append(pet);
        }
    }
    return pets;
}

/**
 * @brief 写回列表配置
 * @author chiangyang
 */
void CustomPetStore::saveList(const QVector<CustomPet> &pets) const {
    QJsonArray array;
    for (const CustomPet &pet : pets) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), pet.id);
        obj.insert(QStringLiteral("name"), pet.name);
        obj.insert(QStringLiteral("fileName"), pet.fileName);
        obj.insert(QStringLiteral("isGif"), pet.isGif);
        array.append(obj);
    }
    ConfigManager::instance()->setValue(
        kPetsConfigKey, QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

/**
 * @brief 获取已导入的自定义宠物列表
 * @author chiangyang
 */
QVector<CustomPet> CustomPetStore::list() const {
    return loadList();
}

/**
 * @brief 按 id 查找自定义宠物
 * @author chiangyang
 */
CustomPet CustomPetStore::find(const QString &id) const {
    for (const CustomPet &pet : loadList()) {
        if (pet.id == id) {
            return pet;
        }
    }
    return CustomPet();
}

/**
 * @brief 获取记录的素材文件绝对路径
 * @author chiangyang
 */
QString CustomPetStore::filePath(const CustomPet &pet) const {
    return QDir(petsDir()).filePath(pet.fileName);
}

/**
 * @brief 导入一张自定义宠物素材
 * @author chiangyang
 */
CustomPet CustomPetStore::add(const QString &sourcePath, QString *errorMessage) {
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return CustomPet();
    };

    const QFileInfo source(sourcePath);
    if (!source.exists() || !source.isFile()) {
        return fail(QStringLiteral("source file not found"));
    }

    const QString suffix = source.suffix().toLower();
    const bool isGif = (suffix == QStringLiteral("gif"));
    const bool isStatic = (suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg")
                           || suffix == QStringLiteral("jpeg") || suffix == QStringLiteral("webp"));
    if (!isGif && !isStatic) {
        return fail(QStringLiteral("unsupported format"));
    }

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString dir = petsDir();

    CustomPet pet;
    pet.id = id;
    pet.name = source.completeBaseName();
    pet.isGif = isGif;

    if (isGif) {
        if (source.size() > kMaxGifBytes) {
            return fail(QStringLiteral("gif too large"));
        }
        // 文件体积不等于帧内存占用：QMovie 全帧缓存下高分辨率帧同样打爆内存，
        // 解码前用文件头核对帧尺寸（size() 只读头不解码像素）
        QImageReader gifReader(source.absoluteFilePath());
        const QSize frame = gifReader.size();
        if (frame.isValid() && qMax(frame.width(), frame.height()) > kMaxGifEdge) {
            return fail(QStringLiteral("gif frame too large"));
        }
        pet.fileName = id + QStringLiteral(".gif");
        if (!QFile::copy(source.absoluteFilePath(), QDir(dir).filePath(pet.fileName))) {
            return fail(QStringLiteral("copy failed"));
        }
    } else {
        // 先读文件头判定尺寸，再决定解码方式：直接 QImage(path) 会把亿级像素
        // 原图整张解码进内存才检查上限
        QImageReader reader(source.absoluteFilePath());
        const QSize header = reader.size();
        if (header.isValid()) {
            const int longest = qMax(header.width(), header.height());
            if (longest > kMaxDecodeEdge) {
                return fail(QStringLiteral("image too large"));
            }
            if (longest > kMaxStaticEdge) {
                // 解码前声明目标尺寸：JPEG 原生支持读取时降采样，避免整张解码
                reader.setScaledSize(header.scaled(kMaxStaticEdge, kMaxStaticEdge,
                                                   Qt::KeepAspectRatio));
            }
        }
        QImage image = reader.read();
        if (image.isNull()) {
            return fail(QStringLiteral("image decode failed"));
        }
        const int longest = qMax(image.width(), image.height());
        if (longest > kMaxStaticEdge) {
            // 格式不支持读取时缩放的兜底（Qt 对不支持的 setScaledSize 会忽略）
            image = image.scaled(kMaxStaticEdge, kMaxStaticEdge, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
        }
        pet.fileName = id + QStringLiteral(".png");
        if (!image.save(QDir(dir).filePath(pet.fileName), "PNG")) {
            return fail(QStringLiteral("save failed"));
        }
    }

    QVector<CustomPet> pets = loadList();
    pets.append(pet);
    saveList(pets);
    LOG_INFO(QString("Custom pet imported: %1 -> %2").arg(sourcePath, pet.fileName));
    return pet;
}

/**
 * @brief 删除自定义宠物（同时删除入库文件）
 * @author chiangyang
 */
bool CustomPetStore::remove(const QString &id) {
    QVector<CustomPet> pets = loadList();
    CustomPet target;
    for (int i = 0; i < pets.size(); ++i) {
        if (pets[i].id == id) {
            target = pets.takeAt(i);
            break;
        }
    }
    if (!target.isValid()) {
        return false;
    }
    QFile::remove(filePath(target));
    saveList(pets);

    // 删除的是当前选中项时回落到默认内置样式
    if (isCustomSelection(selection()) && customIdFromSelection(selection()) == id) {
        setSelection(builtinSelection(0));
    }
    LOG_INFO(QString("Custom pet removed: %1").arg(id));
    return true;
}

/**
 * @brief 获取当前选择
 * @author chiangyang
 */
QString CustomPetStore::selection() const {
    ConfigManager *config = ConfigManager::instance();
    QString current = config->value(kSelectionKey).toString();
    if (!current.isEmpty()) {
        return current;
    }
    // 旧配置迁移：历史版本只有 pet/style（内置样式枚举值）
    const int legacy = config->value(kLegacyStyleKey, 0).toInt();
    current = builtinSelection(legacy);
    config->setValue(kSelectionKey, current);
    return current;
}

/**
 * @brief 持久化当前选择
 * @author chiangyang
 */
void CustomPetStore::setSelection(const QString &selection) {
    ConfigManager::instance()->setValue(kSelectionKey, selection);
}
