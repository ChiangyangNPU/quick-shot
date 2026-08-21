#ifndef OCR_ENGINE_H
#define OCR_ENGINE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QPolygonF>
#include <QImage>
#include <memory>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QFuture>
#include <QMutex>

#ifdef ENABLE_OCR
#include <onnxruntime_cxx_api.h>
#endif

// 平台相关头文件，用于动态加载 GPU 执行提供者
#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MAC)
#include <dlfcn.h>
#endif

// DirectML 执行提供者函数指针类型定义（Windows）
#ifdef Q_OS_WIN
typedef OrtStatus* (ORT_API_CALL *PFN_OrtSessionOptionsAppendExecutionProvider_DML)(OrtSessionOptions* options, int device_id);
#endif

/**
 * @brief OCR 引擎类
 *
 * 单例模式，管理 ONNX Runtime 会话和模型加载。
 * 使用 PP-OCRv4 mobile 模型进行多语言文字识别。
 * 支持 GPU 加速（Windows DirectML / macOS CoreML），通过配置项 ocr/useGpu 控制。
 * GPU 执行提供者采用动态加载机制，加载失败时自动回退到 CPU。
 * @author chiangyang
 */
class OcrEngine : public QObject {
    Q_OBJECT

public:
    /**
     * @brief OCR 识别语言枚举
     * @author chiangyang
     */
    enum class OcrLanguage {
        ChineseEnglish,  ///< 中英文
        English,         ///< 英文
        Japanese,        ///< 日文
        Korean,          ///< 韩文
        Multilingual     ///< 多语言
    };

    /**
     * @brief OCR 识别结果结构体
     * @author chiangyang
     */
    struct OcrResult {
        QStringList texts;           ///< 识别的文本列表
        QVector<float> scores;       ///< 每个文本的置信度
        QVector<QPolygonF> polygons; ///< 检测到的文本区域多边形
    };

    /**
     * @brief 获取单例实例
     * @return OcrEngine 实例指针
     * @author chiangyang
     */
    static OcrEngine* instance();

    /**
     * @brief 初始化 OCR 引擎
     * @param modelDir 模型目录路径（包含 det.onnx、rec.onnx 和字典文件）
     * @return 是否初始化成功
     * @author chiangyang
     */
    bool initialize(const QString &modelDir);

    /**
     * @brief 检查引擎是否就绪
     * @return 是否已加载模型
     * @author chiangyang
     */
    bool isReady() const;

    /**
     * @brief 释放 OCR 引擎资源
     *
     * 释放 ONNX Runtime 会话和模型资源，释放后 isReady() 返回 false。
     * 如果当前正在识别中，会延迟到识别结束后释放。
     * 下次调用 recognize() 时会自动重新初始化。
     * @author chiangyang
     */
    void release();

    /**
     * @brief 检查是否正在识别中
     * @return 是否正在执行 OCR 识别
     * @author chiangyang
     */
    bool isRecognizing() const;

    /**
     * @brief 对图像进行 OCR 识别（同步方法）
     * @param image 输入图像
     * @return OCR 识别结果
     * @author chiangyang
     */
    OcrResult recognize(const QImage &image);

    /**
     * @brief 异步对图像进行 OCR 识别
     *
     * 使用 QtConcurrent 在工作线程中执行识别，完成后通过
     * recognitionFinished 信号通知结果。
     *
     * @param image 输入图像
     * @author chiangyang
     */
    void recognizeAsync(const QImage &image);

    /**
     * @brief 获取最近一次异步识别的结果
     * @return OCR 识别结果（如果还在识别中则返回空结果）
     * @author chiangyang
     */
    OcrResult lastResult() const { return m_lastResult; }

    /**
     * @brief 切换 OCR 识别语言
     * @param lang 目标语言
     * @return 是否切换成功
     * @author chiangyang
     */
    bool switchLanguage(OcrLanguage lang);

    /**
     * @brief 获取当前识别语言
     * @return 当前语言枚举值
     * @author chiangyang
     */
    OcrLanguage currentLanguage() const;

    /**
     * @brief 从配置字符串获取语言枚举
     * @param langKey 语言配置键（ch_en, en, ja, ko, multi）
     * @return 语言枚举值
     * @author chiangyang
     */
    static OcrLanguage languageFromKey(const QString &langKey);

    /**
     * @brief 获取语言对应的配置键
     * @param lang 语言枚举值
     * @return 配置键字符串
     * @author chiangyang
     */
    static QString languageToKey(OcrLanguage lang);

    /**
     * @brief 判断当前是否使用 GPU 推理
     * @return 是否正在使用 GPU 加速
     * @author chiangyang
     */
    bool isUsingGpu() const { return m_useGpu; }

signals:
    /**
     * @brief OCR 识别完成信号
     * @param result 识别结果
     * @author chiangyang
     */
    void recognitionFinished(const OcrResult &result);

    /**
     * @brief OCR 识别错误信号
     * @param errorMessage 错误消息
     * @author chiangyang
     */
    void recognitionError(const QString &errorMessage);

private:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit OcrEngine(QObject *parent = nullptr);

#ifdef ENABLE_OCR
    /**
     * @brief 加载字符字典
     * @param dictPath 字典文件路径
     * @return 是否加载成功
     * @author chiangyang
     */
    bool loadDict(const QString &dictPath);

    /**
     * @brief 获取语言对应的识别模型文件名
     * @param lang 语言枚举值
     * @return 模型文件名
     * @author chiangyang
     */
    static QString recModelName(OcrLanguage lang);

    /**
     * @brief 获取语言对应的字典文件名
     * @param lang 语言枚举值
     * @return 字典文件名
     * @author chiangyang
     */
    static QString dictFileName(OcrLanguage lang);

    /**
     * @brief 配置 GPU 执行提供者
     *
     * 根据配置项 ocr/useGpu 决定是否启用 GPU 加速。
     * Windows 平台使用 DirectML，macOS 平台使用 CoreML。
     * GPU 加载失败时自动回退到 CPU。
     * @return 是否成功启用 GPU
     * @author chiangyang
     */
    bool configureGpuProvider();

    /**
     * @brief 释放 GPU 执行提供者资源
     * @author chiangyang
     */
    void releaseGpuProvider();

#ifdef Q_OS_WIN
    /**
     * @brief 动态加载 DirectML 执行提供者
     *
     * 运行时加载 DirectML.dll，避免在无 GPU/DirectX 12 的机器上启动失败。
     * @return 是否加载成功
     * @author chiangyang
     */
    bool loadDirectML();

    /**
     * @brief 卸载 DirectML 动态库
     * @author chiangyang
     */
    void unloadDirectML();
#endif

    std::unique_ptr<Ort::Env> m_env;           ///< ONNX Runtime 环境
    std::unique_ptr<Ort::Session> m_detSession; ///< 检测模型会话
    std::unique_ptr<Ort::Session> m_recSession; ///< 识别模型会话
    std::unique_ptr<Ort::SessionOptions> m_sessionOptions; ///< 会话选项（使用指针以便干净重建）

    // GPU 动态加载相关
    bool m_useGpu = false;                      ///< 当前是否使用 GPU 推理

#ifdef Q_OS_WIN
    static PFN_OrtSessionOptionsAppendExecutionProvider_DML g_ortDmlFunc; ///< DirectML 函数指针
    static HMODULE g_directmlDll;               ///< DirectML 动态库句柄
#endif
#endif

    static OcrEngine* s_instance;              ///< 单例实例
    QStringList m_charDict;                     ///< 字符字典
    bool m_ready = false;                       ///< 是否已就绪
    bool m_isRecognizing = false;               ///< 是否正在识别中
    bool m_pendingRelease = false;              ///< 是否有待释放（识别中时设置）
    OcrLanguage m_language = OcrLanguage::ChineseEnglish; ///< 当前语言
    QString m_modelDir;                         ///< 模型目录路径
    QFutureWatcher<OcrResult>* m_watcher = nullptr; ///< 异步识别的 FutureWatcher
    OcrResult m_lastResult;                     ///< 最近一次识别结果
    mutable QMutex m_mutex;                    ///< 互斥锁，保护线程安全
};

#endif // OCR_ENGINE_H
