#include "OcrEngine.h"
#include "Logger.h"
#include "ConfigManager.h"

#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QCoreApplication>

#ifdef ENABLE_OCR
#include "OcrPreprocess.h"
#include "OcrDetPostprocess.h"
#include "OcrRecPostprocess.h"

// DirectML 静态成员定义
#ifdef Q_OS_WIN
PFN_OrtSessionOptionsAppendExecutionProvider_DML OcrEngine::g_ortDmlFunc = nullptr;
HMODULE OcrEngine::g_directmlDll = nullptr;
#endif
#endif

OcrEngine* OcrEngine::s_instance = nullptr;

/**
 * @brief 获取单例实例
 * @return OcrEngine 实例指针
 * @author chiangyang
 */
OcrEngine* OcrEngine::instance() {
    if (!s_instance) {
        s_instance = new OcrEngine();
    }
    return s_instance;
}

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
OcrEngine::OcrEngine(QObject *parent) : QObject(parent) {
}

/**
 * @brief 从配置字符串获取语言枚举
 * @param langKey 语言配置键（ch_en, en, ja, ko, multi）
 * @return 语言枚举值
 * @author chiangyang
 */
OcrEngine::OcrLanguage OcrEngine::languageFromKey(const QString &langKey) {
    if (langKey == "en") return OcrLanguage::English;
    if (langKey == "ja") return OcrLanguage::Japanese;
    if (langKey == "ko") return OcrLanguage::Korean;
    if (langKey == "multi") return OcrLanguage::Multilingual;
    return OcrLanguage::ChineseEnglish;
}

/**
 * @brief 获取语言对应的配置键
 * @param lang 语言枚举值
 * @return 配置键字符串
 * @author chiangyang
 */
QString OcrEngine::languageToKey(OcrLanguage lang) {
    switch (lang) {
        case OcrLanguage::English: return "en";
        case OcrLanguage::Japanese: return "ja";
        case OcrLanguage::Korean: return "ko";
        case OcrLanguage::Multilingual: return "multi";
        default: return "ch_en";
    }
}

#ifdef ENABLE_OCR
/**
 * @brief 获取语言对应的识别模型文件名
 * @param lang 语言枚举值
 * @return 模型文件名
 * @author chiangyang
 */
QString OcrEngine::recModelName(OcrLanguage lang) {
    switch (lang) {
        case OcrLanguage::English: return "en_PP-OCRv4_mobile_rec.onnx";
        case OcrLanguage::Japanese: return "japan_PP-OCRv4_mobile_rec.onnx";
        case OcrLanguage::Korean: return "korean_PP-OCRv4_mobile_rec.onnx";
        default: return "ch_PP-OCRv4_mobile_rec.onnx";
    }
}

/**
 * @brief 获取语言对应的字典文件名
 * @param lang 语言枚举值
 * @return 字典文件名
 * @author chiangyang
 */
QString OcrEngine::dictFileName(OcrLanguage lang) {
    switch (lang) {
        case OcrLanguage::English: return "en_dict_ppocrv4.txt";
        case OcrLanguage::Japanese: return "japan_dict.txt";
        case OcrLanguage::Korean: return "korean_dict.txt";
        default: return "ppocrv4_dict.txt";
    }
}

#ifdef Q_OS_WIN
/**
 * @brief 动态加载 DirectML 执行提供者
 *
 * 运行时加载 DirectML.dll，避免在无 GPU/DirectX 12 的机器上启动时 0xc0000142 崩溃。
 * 加载成功后通过 GetProcAddress 获取 OrtSessionOptionsAppendExecutionProvider_DML 函数。
 * @return 是否加载成功
 * @author chiangyang
 */
bool OcrEngine::loadDirectML() {
    if (g_ortDmlFunc && g_directmlDll) {
        return true;
    }

    // 依次尝试：应用目录、当前目录、系统 PATH
    QString dllPath = QCoreApplication::applicationDirPath() + "/DirectML.dll";
    g_directmlDll = LoadLibraryW(dllPath.toStdWString().c_str());

    if (!g_directmlDll) {
        g_directmlDll = LoadLibraryW(L"DirectML.dll");
    }

    if (!g_directmlDll) {
        LOG_WARNING("OcrEngine: DirectML.dll not found in application dir or PATH");
        return false;
    }

    g_ortDmlFunc = (PFN_OrtSessionOptionsAppendExecutionProvider_DML)
        GetProcAddress(g_directmlDll, "OrtSessionOptionsAppendExecutionProvider_DML");

    if (!g_ortDmlFunc) {
        LOG_WARNING("OcrEngine: DirectML.dll loaded but OrtSessionOptionsAppendExecutionProvider_DML not found");
        FreeLibrary(g_directmlDll);
        g_directmlDll = nullptr;
        return false;
    }

    LOG_INFO("OcrEngine: DirectML.dll loaded successfully");
    return true;
}

/**
 * @brief 卸载 DirectML 动态库
 * @author chiangyang
 */
void OcrEngine::unloadDirectML() {
    if (g_directmlDll) {
        FreeLibrary(g_directmlDll);
        g_directmlDll = nullptr;
        g_ortDmlFunc = nullptr;
        LOG_INFO("OcrEngine: DirectML.dll unloaded");
    }
}
#endif // Q_OS_WIN

/**
 * @brief 配置 GPU 执行提供者
 *
 * 根据配置项 ocr/useGpu 决定是否启用 GPU 加速。
 * Windows 平台使用 DirectML（动态加载 DirectML.dll），
 * macOS 平台使用 CoreML（动态查找 OrtSessionOptionsAppendExecutionProvider_CoreML 符号）。
 * GPU 加载或初始化失败时自动回退到 CPU，不影响 OCR 功能。
 * @return 是否成功启用 GPU
 * @author chiangyang
 */
bool OcrEngine::configureGpuProvider() {
    bool useGpu = ConfigManager::instance()->value("ocr/useGpu", false).toBool();
    if (!useGpu) {
        LOG_INFO("OcrEngine: GPU acceleration disabled by config (ocr/useGpu=false), using CPU");
        return false;
    }

    LOG_INFO("OcrEngine: GPU acceleration enabled by config (ocr/useGpu=true), trying to initialize...");

    if (!m_sessionOptions) {
        LOG_ERROR("OcrEngine: SessionOptions not created, cannot configure GPU provider");
        return false;
    }

#ifdef Q_OS_WIN
    // ============ Windows: DirectML ============
    if (!loadDirectML()) {
        LOG_WARNING("OcrEngine: DirectML.dll not available, falling back to CPU");
        m_useGpu = false;
        return false;
    }

    OrtSessionOptions* rawOptions = m_sessionOptions->operator OrtSessionOptions*();
    OrtStatus* status = g_ortDmlFunc(rawOptions, 0);
    if (status) {
        Ort::Status ortStatus(status);
        LOG_WARNING(QString("OcrEngine: DirectML initialization failed: %1, falling back to CPU")
            .arg(QString::fromStdString(ortStatus.GetErrorMessage())));
        unloadDirectML();
        m_useGpu = false;
        return false;
    }

    LOG_INFO("OcrEngine: DirectML execution provider enabled (device_id=0)");
    m_useGpu = true;
    return true;

#elif defined(Q_OS_MAC)
    // ============ macOS: CoreML（动态加载） ============
    typedef OrtStatus* (ORT_API_CALL *PFN_AppendCoreML)(OrtSessionOptions* options, uint32_t coreml_flags);
    PFN_AppendCoreML pfnCoreML = (PFN_AppendCoreML)dlsym(RTLD_DEFAULT, "OrtSessionOptionsAppendExecutionProvider_CoreML");

    if (!pfnCoreML) {
        LOG_WARNING("OcrEngine: CoreML provider not available (dlsym returned NULL), falling back to CPU");
        m_useGpu = false;
        return false;
    }

    // 策略：优先尝试 GPU 加速（flags=2: ORT_COREML_FLAG_USE_GPU_ONLY），
    // 失败则回退到自动选择（flags=0: ORT_COREML_FLAG_USE_NONE），
    // 让 CoreML 自动在 Neural Engine / GPU / CPU 中选择最优设备。
    OrtSessionOptions* rawOptions = m_sessionOptions->operator OrtSessionOptions*();
    uint32_t coremlFlags = 2; // ORT_COREML_FLAG_USE_GPU_ONLY
    OrtStatus* status = pfnCoreML(rawOptions, coremlFlags);

    if (status) {
        Ort::Status ortStatus(status);
        QString errMsg = QString::fromStdString(ortStatus.GetErrorMessage());
        LOG_WARNING(QString("OcrEngine: CoreML GPU-only init failed: %1, falling back to auto mode")
            .arg(errMsg));

        coremlFlags = 0; // ORT_COREML_FLAG_USE_NONE
        status = pfnCoreML(rawOptions, coremlFlags);

        if (status) {
            Ort::Status ortStatus2(status);
            LOG_WARNING(QString("OcrEngine: CoreML auto init also failed: %1, falling back to CPU")
                .arg(QString::fromStdString(ortStatus2.GetErrorMessage())));
            m_useGpu = false;
            return false;
        }

        LOG_INFO("OcrEngine: CoreML (auto-select) execution provider enabled");
        m_useGpu = true;
        return true;
    }

    LOG_INFO("OcrEngine: CoreML GPU execution provider enabled");
    m_useGpu = true;
    return true;

#else
    LOG_WARNING("OcrEngine: GPU acceleration not supported on this platform, using CPU");
    m_useGpu = false;
    return false;
#endif
}

/**
 * @brief 释放 GPU 执行提供者资源
 *
 * 卸载 DirectML 动态库（Windows），重置 GPU 状态标志。
 * macOS CoreML 无需显式释放（符号查找，无额外资源）。
 * @author chiangyang
 */
void OcrEngine::releaseGpuProvider() {
#ifdef Q_OS_WIN
    unloadDirectML();
#endif
    m_useGpu = false;
    LOG_INFO("OcrEngine: GPU provider released");
}
#endif // ENABLE_OCR

/**
 * @brief 初始化 OCR 引擎
 * @param modelDir 模型目录路径
 * @return 是否初始化成功
 * @author chiangyang
 */
bool OcrEngine::initialize(const QString &modelDir) {
    // 如果已初始化，先释放资源
    if (m_ready) {
        release();
    }

#ifndef ENABLE_OCR
    Q_UNUSED(modelDir);
    LOG_WARNING("OcrEngine: ONNX Runtime not available, OCR disabled");
    return false;
#else
    LOG_INFO(QString("OcrEngine: Initializing with model dir: %1").arg(modelDir));

    m_modelDir = modelDir;
    QDir dir(modelDir + "/mobile");
    if (!dir.exists()) {
        LOG_ERROR(QString("OcrEngine: Mobile model directory does not exist: %1").arg(dir.path()));
        return false;
    }

    // 从配置读取语言设置
    QString langKey = ConfigManager::instance()->value("ocr/language", "ch_en").toString();
    m_language = languageFromKey(langKey);
    LOG_INFO(QString("OcrEngine: Language from config: %1").arg(langKey));

    // 检测模型（语言无关）
    QString detModelPath = dir.filePath("det_mobile.onnx");
    // 识别模型和字典（语言相关）
    QString recModelPath = dir.filePath(recModelName(m_language));
    QString dictPath = dir.filePath(dictFileName(m_language));

    if (!QFile::exists(detModelPath)) {
        LOG_ERROR(QString("OcrEngine: Detection model not found: %1").arg(detModelPath));
        return false;
    }
    if (!QFile::exists(recModelPath)) {
        LOG_ERROR(QString("OcrEngine: Recognition model not found: %1").arg(recModelPath));
        return false;
    }
    if (!QFile::exists(dictPath)) {
        LOG_ERROR(QString("OcrEngine: Dictionary file not found: %1").arg(dictPath));
        return false;
    }

    try {
        // 创建 ONNX Runtime 环境
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "QuickShot-OCR");

        // 配置会话选项
        // 创建全新的会话选项（使用 unique_ptr 确保每次初始化干净）
        m_sessionOptions = std::make_unique<Ort::SessionOptions>();
        m_sessionOptions->SetIntraOpNumThreads(4);
        m_sessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // 配置 GPU 执行提供者（失败自动回退 CPU，不中断初始化）
        bool gpuEnabled = configureGpuProvider();

        // 加载检测模型
#ifdef Q_OS_WIN
        m_detSession = std::make_unique<Ort::Session>(*m_env, detModelPath.toStdWString().c_str(), *m_sessionOptions);
        m_recSession = std::make_unique<Ort::Session>(*m_env, recModelPath.toStdWString().c_str(), *m_sessionOptions);
#else
        m_detSession = std::make_unique<Ort::Session>(*m_env, detModelPath.toUtf8().constData(), *m_sessionOptions);
        m_recSession = std::make_unique<Ort::Session>(*m_env, recModelPath.toUtf8().constData(), *m_sessionOptions);
#endif

        // 加载字符字典
        if (!loadDict(dictPath)) {
            LOG_ERROR("OcrEngine: Failed to load character dictionary");
            return false;
        }

        // 打印模型信息
        Ort::AllocatorWithDefaultOptions alloc;

        // 检测模型
        size_t detInputCount = m_detSession->GetInputCount();
        size_t detOutputCount = m_detSession->GetOutputCount();
        LOG_INFO(QString("OcrEngine: Det model - %1 inputs, %2 outputs").arg(detInputCount).arg(detOutputCount));
        for (size_t i = 0; i < detInputCount; ++i) {
            auto name = m_detSession->GetInputNameAllocated(i, alloc);
            auto typeInfo = m_detSession->GetInputTypeInfo(i);
            auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
            auto shape = tensorInfo.GetShape();
            QString shapeStr;
            for (auto s : shape) shapeStr += QString("%1 ").arg(s);
            LOG_INFO(QString("OcrEngine: Det input %1: name=%2, shape=[%3]")
                .arg(i).arg(name.get()).arg(shapeStr.trimmed()));
        }
        for (size_t i = 0; i < detOutputCount; ++i) {
            auto name = m_detSession->GetOutputNameAllocated(i, alloc);
            auto typeInfo = m_detSession->GetOutputTypeInfo(i);
            auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
            auto shape = tensorInfo.GetShape();
            QString shapeStr;
            for (auto s : shape) shapeStr += QString("%1 ").arg(s);
            LOG_INFO(QString("OcrEngine: Det output %1: name=%2, shape=[%3]")
                .arg(i).arg(name.get()).arg(shapeStr.trimmed()));
        }

        // 识别模型
        size_t recInputCount = m_recSession->GetInputCount();
        size_t recOutputCount = m_recSession->GetOutputCount();
        LOG_INFO(QString("OcrEngine: Rec model - %1 inputs, %2 outputs").arg(recInputCount).arg(recOutputCount));
        for (size_t i = 0; i < recInputCount; ++i) {
            auto name = m_recSession->GetInputNameAllocated(i, alloc);
            auto typeInfo = m_recSession->GetInputTypeInfo(i);
            auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
            auto shape = tensorInfo.GetShape();
            QString shapeStr;
            for (auto s : shape) shapeStr += QString("%1 ").arg(s);
            LOG_INFO(QString("OcrEngine: Rec input %1: name=%2, shape=[%3]")
                .arg(i).arg(name.get()).arg(shapeStr.trimmed()));
        }
        for (size_t i = 0; i < recOutputCount; ++i) {
            auto name = m_recSession->GetOutputNameAllocated(i, alloc);
            auto typeInfo = m_recSession->GetOutputTypeInfo(i);
            auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
            auto shape = tensorInfo.GetShape();
            QString shapeStr;
            for (auto s : shape) shapeStr += QString("%1 ").arg(s);
            LOG_INFO(QString("OcrEngine: Rec output %1: name=%2, shape=[%3]")
                .arg(i).arg(name.get()).arg(shapeStr.trimmed()));
        }

        m_ready = true;
        LOG_INFO(QString("OcrEngine: Initialized successfully, language=%1, dict size=%2, GPU=%3")
            .arg(languageToKey(m_language)).arg(m_charDict.size())
            .arg(m_useGpu ? QStringLiteral("enabled") : QStringLiteral("disabled")));
        return true;

    } catch (const Ort::Exception &e) {
        LOG_ERROR(QString("OcrEngine: ONNX Runtime error: %1").arg(e.what()));
        releaseGpuProvider();
        return false;
    } catch (const std::exception &e) {
        LOG_ERROR(QString("OcrEngine: Initialization error: %1").arg(e.what()));
        releaseGpuProvider();
        return false;
    }
#endif
}

/**
 * @brief 切换 OCR 识别语言
 * @param lang 目标语言
 * @return 是否切换成功
 * @author chiangyang
 */
bool OcrEngine::switchLanguage(OcrLanguage lang) {
#ifndef ENABLE_OCR
    Q_UNUSED(lang);
    LOG_WARNING("OcrEngine: ONNX Runtime not available, cannot switch language");
    return false;
#else
    if (lang == m_language && m_ready) {
        LOG_INFO(QString("OcrEngine: Language already set to %1, no switch needed").arg(languageToKey(lang)));
        return true;
    }

    // 尚未初始化，只记录语言设置，等 initialize 时再加载
    if (m_modelDir.isEmpty() || !m_sessionOptions) {
        m_language = lang;
        LOG_INFO(QString("OcrEngine: Engine not initialized, language set to %1 (deferred)").arg(languageToKey(lang)));
        return true;
    }

    LOG_INFO(QString("OcrEngine: Switching language from %1 to %2")
        .arg(languageToKey(m_language)).arg(languageToKey(lang)));

    QDir dir(m_modelDir + "/mobile");
    if (!dir.exists()) {
        LOG_ERROR(QString("OcrEngine: Mobile model directory does not exist: %1").arg(dir.path()));
        return false;
    }

    QString recModelPath = dir.filePath(recModelName(lang));
    QString dictPath = dir.filePath(dictFileName(lang));

    if (!QFile::exists(recModelPath)) {
        LOG_ERROR(QString("OcrEngine: Recognition model not found: %1").arg(recModelPath));
        return false;
    }
    if (!QFile::exists(dictPath)) {
        LOG_ERROR(QString("OcrEngine: Dictionary file not found: %1").arg(dictPath));
        return false;
    }

    try {
        // 重新加载识别模型
#ifdef Q_OS_WIN
        m_recSession = std::make_unique<Ort::Session>(*m_env, recModelPath.toStdWString().c_str(), *m_sessionOptions);
#else
        m_recSession = std::make_unique<Ort::Session>(*m_env, recModelPath.toUtf8().constData(), *m_sessionOptions);
#endif

        // 重新加载字典
        if (!loadDict(dictPath)) {
            LOG_ERROR("OcrEngine: Failed to load character dictionary");
            return false;
        }

        m_language = lang;
        LOG_INFO(QString("OcrEngine: Language switched to %1, dict size=%2, GPU=%3")
            .arg(languageToKey(m_language)).arg(m_charDict.size())
            .arg(m_useGpu ? "enabled" : "disabled"));
        return true;

    } catch (const Ort::Exception &e) {
        LOG_ERROR(QString("OcrEngine: ONNX Runtime error during language switch: %1").arg(e.what()));
        return false;
    } catch (const std::exception &e) {
        LOG_ERROR(QString("OcrEngine: Language switch error: %1").arg(e.what()));
        return false;
    }
#endif
}

/**
 * @brief 获取当前识别语言
 * @return 当前语言枚举值
 * @author chiangyang
 */
OcrEngine::OcrLanguage OcrEngine::currentLanguage() const {
    return m_language;
}

/**
 * @brief 检查引擎是否就绪
 * @return 是否已加载模型
 * @author chiangyang
 */
bool OcrEngine::isReady() const {
    return m_ready;
}

/**
 * @brief 释放 OCR 引擎资源
 *
 * 释放 ONNX Runtime 会话和模型资源，释放后 isReady() 返回 false。
 * 如果当前正在识别中，会延迟到识别结束后释放。
 * 下次调用 recognize() 时会自动重新初始化。
 * @author chiangyang
 */
void OcrEngine::release() {
    // 如果正在识别中，延迟释放
    if (m_isRecognizing) {
        LOG_INFO("OcrEngine: Recognition in progress, deferring release");
        m_pendingRelease = true;
        return;
    }

    LOG_INFO("OcrEngine: Releasing resources");

#ifdef ENABLE_OCR
    m_detSession.reset();
    m_recSession.reset();
    m_env.reset();
    m_sessionOptions.reset();
    releaseGpuProvider();
#endif

    m_ready = false;
    LOG_INFO("OcrEngine: Resources released");
}

/**
 * @brief 检查是否正在识别中
 * @return 是否正在执行 OCR 识别
 * @author chiangyang
 */
bool OcrEngine::isRecognizing() const {
    return m_isRecognizing;
}

/**
 * @brief 对图像进行 OCR 识别
 * @param image 输入图像
 * @return OCR 识别结果
 * @author chiangyang
 */
OcrEngine::OcrResult OcrEngine::recognize(const QImage &image) {
    QMutexLocker locker(&m_mutex);
    OcrResult result;

#ifndef ENABLE_OCR
    Q_UNUSED(image);
    LOG_WARNING("OcrEngine: ONNX Runtime not available, OCR disabled");
    return result;
#else
    if (!m_ready) {
        LOG_WARNING("OcrEngine: Engine not ready, attempting initialization");
        // 尝试从配置获取模型路径
        QString modelPath = ConfigManager::instance()->value("ocr/modelPath", "").toString();
        if (modelPath.isEmpty()) {
            modelPath = QCoreApplication::applicationDirPath() + "/models/ocr";
        }
        if (!initialize(modelPath)) {
            LOG_ERROR("OcrEngine: Failed to initialize on recognize");
            return result;
        }
    }

    // 设置识别中状态
    m_isRecognizing = true;

    try {
        // 1. 预处理：检测
        LOG_INFO(QString("OcrEngine: Input image size: %1x%2, GPU=%3")
            .arg(image.width()).arg(image.height())
            .arg(m_useGpu ? "enabled" : "disabled"));

        auto detInput = OcrPreprocess::preprocessForDet(image);
        LOG_INFO(QString("OcrEngine: Det preprocessed: %1x%2, data size: %3")
            .arg(detInput.resizedW).arg(detInput.resizedH).arg(detInput.data.size()));
        // 2. 运行检测模型
        Ort::AllocatorWithDefaultOptions allocator;
        auto detInputName = m_detSession->GetInputNameAllocated(0, allocator);
        auto detOutputName = m_detSession->GetOutputNameAllocated(0, allocator);

        std::vector<int64_t> detInputShape = {1, 3, detInput.resizedH, detInput.resizedW};
        Ort::Value detInputTensor = Ort::Value::CreateTensor<float>(
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault),
            detInput.data.data(), detInput.data.size(),
            detInputShape.data(), detInputShape.size());

        const char* detInputNameStr = detInputName.get();
        const char* detOutputNameStr = detOutputName.get();

        auto detOutput = m_detSession->Run(Ort::RunOptions{nullptr},
            &detInputNameStr, &detInputTensor, 1,
            &detOutputNameStr, 1);

        // 3. 检测后处理：获取文本区域
        const float* detOutputData = detOutput[0].GetTensorMutableData<float>();
        auto detOutputShape = detOutput[0].GetTensorTypeAndShapeInfo().GetShape();
        int outH = static_cast<int>(detOutputShape[2]);
        int outW = static_cast<int>(detOutputShape[3]);

        // 打印检测输出的统计信息
        float minVal = 1e9, maxVal = -1e9, sumVal = 0;
        int totalPixels = outH * outW;
        for (int i = 0; i < totalPixels; ++i) {
            if (detOutputData[i] < minVal) minVal = detOutputData[i];
            if (detOutputData[i] > maxVal) maxVal = detOutputData[i];
            sumVal += detOutputData[i];
        }
        LOG_INFO(QString("OcrEngine: Det output shape: [%1, %2], min=%3, max=%4, avg=%5")
            .arg(outH).arg(outW).arg(minVal).arg(maxVal).arg(sumVal / totalPixels));

        auto boxes = OcrDetPostprocess::process(detOutputData, outH, outW,
            image.width(), image.height(),
            detInput.resizedW, detInput.resizedH);

        LOG_INFO(QString("OcrEngine: Detected %1 text boxes").arg(boxes.size()));

        if (boxes.isEmpty()) {
            LOG_INFO("OcrEngine: No text regions detected");
            return result;
        }

        // 4. 对每个检测区域进行识别
        auto recInputName = m_recSession->GetInputNameAllocated(0, allocator);
        auto recOutputName = m_recSession->GetOutputNameAllocated(0, allocator);
        const char* recInputNameStr = recInputName.get();
        const char* recOutputNameStr = recOutputName.get();

        for (int i = 0; i < boxes.size(); ++i) {
            const auto &box = boxes[i];
            // 裁剪图像区域
            QRect bbox = box.boundingRect().toRect();
            bbox = bbox.intersected(image.rect());
            if (bbox.width() < 2 || bbox.height() < 2) {
                LOG_INFO(QString("OcrEngine: Box %1 too small (%2x%3), skipped")
                    .arg(i).arg(bbox.width()).arg(bbox.height()));
                continue;
            }

            QImage crop = image.copy(bbox);
            LOG_INFO(QString("OcrEngine: Box %1 crop: %2x%3 at (%4,%5)")
                .arg(i).arg(crop.width()).arg(crop.height()).arg(bbox.x()).arg(bbox.y()));

            // 预处理：识别
            auto recInput = OcrPreprocess::preprocessForRec(crop);

            std::vector<int64_t> recInputShape = {1, 3, recInput.resizedH, recInput.resizedW};
            Ort::Value recInputTensor = Ort::Value::CreateTensor<float>(
                Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault),
                recInput.data.data(), recInput.data.size(),
                recInputShape.data(), recInputShape.size());

            auto recOutput = m_recSession->Run(Ort::RunOptions{nullptr},
                &recInputNameStr, &recInputTensor, 1,
                &recOutputNameStr, 1);

            // CTC 解码
            const float* recOutputData = recOutput[0].GetTensorMutableData<float>();
            auto recOutputShape = recOutput[0].GetTensorTypeAndShapeInfo().GetShape();
            int seqLen = static_cast<int>(recOutputShape[1]);
            int numChars = static_cast<int>(recOutputShape[2]);

            LOG_INFO(QString("OcrEngine: Box %1 rec output shape: [%2, %3], dict size: %4")
                .arg(i).arg(seqLen).arg(numChars).arg(m_charDict.size()));

            auto recResult = OcrRecPostprocess::decode(recOutputData, seqLen, numChars, m_charDict);

            LOG_INFO(QString("OcrEngine: Raw rec result: score=%1 text=\"%2\" (seqLen=%3, numChars=%4, dictSize=%5)")
                     .arg(recResult.score, 0, 'f', 3).arg(recResult.text).arg(seqLen).arg(numChars).arg(m_charDict.size()));

            if (!recResult.text.isEmpty() && recResult.score > 0.0f) {
                // 将裁剪坐标映射回原图坐标
                QPolygonF mappedPoly;
                for (const auto &pt : box) {
                    mappedPoly.append(QPointF(pt.x(), pt.y()));
                }
                result.texts.append(recResult.text);
                result.scores.append(recResult.score);
                result.polygons.append(mappedPoly);
            }
        }

        LOG_INFO(QString("OcrEngine: Recognized %1 text regions").arg(result.texts.size()));
        for (int i = 0; i < result.texts.size(); ++i) {
            LOG_INFO(QString("OcrEngine: [%1] score=%2 text=\"%3\"").arg(i).arg(result.scores[i], 0, 'f', 3).arg(result.texts[i]));
        }

    } catch (const Ort::Exception &e) {
        LOG_ERROR(QString("OcrEngine: ONNX Runtime error during recognition: %1").arg(e.what()));
    } catch (const std::exception &e) {
        LOG_ERROR(QString("OcrEngine: Recognition error: %1").arg(e.what()));
    }

    // 重置识别中状态
    m_isRecognizing = false;

    // 如果有待释放请求，现在执行释放
    if (m_pendingRelease) {
        m_pendingRelease = false;
        // 调用实际的释放逻辑（避免递归调用 release()）
#ifdef ENABLE_OCR
        m_detSession.reset();
        m_recSession.reset();
        m_env.reset();
        m_sessionOptions.reset();
        releaseGpuProvider();
#endif
        m_ready = false;
        LOG_INFO("OcrEngine: Pending release executed after recognition");
    }

    return result;
#endif
}

#ifdef ENABLE_OCR
/**
 * @brief 加载字符字典
 * @param dictPath 字典文件路径
 * @return 是否加载成功
 * @author chiangyang
 */
bool OcrEngine::loadDict(const QString &dictPath) {
    m_charDict.clear();

    QFile file(dictPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    // PP-OCR 字典格式：每行一个字符，最后可能有一个 blank 符号
    // index 0 通常是 blank（CTC blank）
    while (!in.atEnd()) {
        QString line = in.readLine();
        // 去掉行尾换行符，但保留空格等空白字符
        if (line.endsWith('\n') || line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        m_charDict.append(line);
    }

    // 确保字典不为空
    if (m_charDict.isEmpty()) {
        return false;
    }

    LOG_INFO(QString("OcrEngine: Loaded %1 characters from dictionary").arg(m_charDict.size()));
    return true;
}
#endif


/**
 * @brief 异步对图像进行 OCR 识别
 *
 * 使用 QtConcurrent 在工作线程中执行识别，完成后通过
 * recognitionFinished 信号通知结果。
 *
 * @param image 输入图像
 * @author chiangyang
 */
void OcrEngine::recognizeAsync(const QImage &image) {
    // 如果正在识别中，跳过
    if (m_isRecognizing) {
        LOG_INFO("OcrEngine: Recognition already in progress, skipping async request");
        return;
    }

    // 创建 QFutureWatcher（如果还没有）
    if (!m_watcher) {
        m_watcher = new QFutureWatcher<OcrResult>(this);
        connect(m_watcher, &QFutureWatcher<OcrResult>::finished, this, [this]() {
            m_lastResult = m_watcher->result();
            m_isRecognizing = false;
            LOG_INFO(QString("OcrEngine: Async recognition finished, found %1 text regions")
                .arg(m_lastResult.texts.size()));
            emit recognitionFinished(m_lastResult);
        });
    }

    // 标记正在识别中
    m_isRecognizing = true;

    // 在工作线程中执行识别
    QFuture<OcrResult> future = QtConcurrent::run([this, image]() {
        return recognize(image);
    });
    m_watcher->setFuture(future);
}
