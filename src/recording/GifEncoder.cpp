#include "GifEncoder.h"

#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace {

/// 5-5-5 量化颜色桶数量（R/G/B 各 5 位）
constexpr int kColorBins = 32 * 32 * 32;

/// LZW 字典哈希表容量（大于 4096，保证字典满之前不会冲突溢出）
constexpr int kHashSize = 5003;

/// LZW 可分配的最大编码（4095，与主流解码器保持一致）
constexpr int LZ_MAX_CODE = 4095;

/**
 * @brief LZW 字典哈希项
 * @author chiangyang
 */
struct HashEntry {
    int key = -1;  ///< (前缀编码 << 8) | 后缀字节，-1 表示空槽
    int code = -1; ///< 该组合对应的输出编码
};

/**
 * @brief 计算哈希槽位
 * @param key 字典键
 * @return 初始槽位
 * @author chiangyang
 */
int hashIndex(int key) {
    int h = (((key & 0xFF) << 4) ^ (key >> 8));
    if (h >= kHashSize) {
        h -= kHashSize;
    }
    return h;
}

/**
 * @brief 在字典中查找组合对应的编码
 * @param table 字典哈希表
 * @param key 字典键
 * @return 找到返回编码，未找到返回 -1
 * @author chiangyang
 */
int findCode(const std::vector<HashEntry> &table, int key) {
    int h = hashIndex(key);
    const int disp = (h == 0) ? 1 : kHashSize - h;
    for (int probe = 0; probe < kHashSize; ++probe) {
        if (table[h].key == key) {
            return table[h].code;
        }
        if (table[h].key == -1) {
            return -1;
        }
        h -= disp;
        if (h < 0) {
            h += kHashSize;
        }
    }
    return -1;
}

/**
 * @brief 把新组合插入字典
 * @param table 字典哈希表
 * @param key 字典键
 * @param code 对应编码
 * @author chiangyang
 */
void insertCode(std::vector<HashEntry> &table, int key, int code) {
    int h = hashIndex(key);
    const int disp = (h == 0) ? 1 : kHashSize - h;
    for (int probe = 0; probe < kHashSize; ++probe) {
        if (table[h].key == -1 || table[h].key == key) {
            table[h].key = key;
            table[h].code = code;
            return;
        }
        h -= disp;
        if (h < 0) {
            h += kHashSize;
        }
    }
}

/**
 * @brief 清空字典
 * @param table 字典哈希表
 * @author chiangyang
 */
void resetTable(std::vector<HashEntry> &table) {
    for (auto &entry : table) {
        entry.key = -1;
        entry.code = -1;
    }
}

/**
 * @brief 判断两帧索引图内容是否完全一致
 *
 * 像素索引与调色板都相同才视为相同帧，用于合并静态画面。
 * @param a 帧 A
 * @param b 帧 B
 * @return 相同返回 true
 * @author chiangyang
 */
bool sameImage(const QImage &a, const QImage &b) {
    if (a.size() != b.size() || a.format() != b.format()) {
        return false;
    }
    const int colorsA = static_cast<int>(a.colorCount());
    const int colorsB = static_cast<int>(b.colorCount());
    if (colorsA != colorsB) {
        return false;
    }
    for (int i = 0; i < colorsA; ++i) {
        if (a.color(i) != b.color(i)) {
            return false;
        }
    }
    const int rowBytes = a.width();
    for (int y = 0; y < a.height(); ++y) {
        if (std::memcmp(a.constScanLine(y), b.constScanLine(y), rowBytes) != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

GifEncoder::GifEncoder() = default;

GifEncoder::~GifEncoder() {
    if (m_opened) {
        abort();
    }
}

bool GifEncoder::open(const QString &filePath, const QSize &size) {
    if (m_opened) {
        abort();
    }
    if (size.isEmpty()) {
        return false;
    }
    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    m_size = size;
    m_pending = QImage();
    m_pendingDelay = 0;
    m_hasPending = false;
    m_frameCount = 0;
    m_opened = true;
    if (!writeHeader()) {
        abort();
        return false;
    }
    return true;
}

bool GifEncoder::writeHeader() {
    QByteArray header;
    header.append("GIF89a", 6);

    // 逻辑屏幕描述符：宽高、无全局色表、颜色分辨率 8 位
    auto appendU16 = [](QByteArray &buf, int value) {
        buf.append(static_cast<char>(value & 0xFF));
        buf.append(static_cast<char>((value >> 8) & 0xFF));
    };
    appendU16(header, m_size.width());
    appendU16(header, m_size.height());
    header.append(static_cast<char>(0x70));  // 无 GCT，颜色分辨率 7+1
    header.append(static_cast<char>(0x00));  // 背景色索引
    header.append(static_cast<char>(0x00));  // 像素宽高比

    // Netscape 应用扩展：无限循环
    header.append(static_cast<char>(0x21));
    header.append(static_cast<char>(0xFF));
    header.append(static_cast<char>(0x0B));
    header.append("NETSCAPE2.0", 11);
    header.append(static_cast<char>(0x03));
    header.append(static_cast<char>(0x01));
    header.append(static_cast<char>(0x00));
    header.append(static_cast<char>(0x00));
    header.append(static_cast<char>(0x00));

    return writeBytes(header);
}

QImage GifEncoder::toIndexed(const QImage &frame) const {
    if (frame.isNull()) {
        return QImage();
    }
    QImage image = frame;
    if (image.size() != m_size) {
        image = image.scaled(m_size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    if (image.format() != QImage::Format_RGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int width = image.width();
    const int height = image.height();

    // 统计 5-5-5 量化后的颜色分布（32768 个桶）
    std::vector<int> counts(kColorBins, 0);
    for (int y = 0; y < height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            const QRgb c = line[x];
            const int key = ((qRed(c) >> 3) << 10) | ((qGreen(c) >> 3) << 5) | (qBlue(c) >> 3);
            ++counts[key];
        }
    }

    // 按出现次数从多到少挑选调色板（最多 256 色）
    std::vector<std::pair<int, int>> used;
    used.reserve(512);
    for (int key = 0; key < kColorBins; ++key) {
        if (counts[key] > 0) {
            used.emplace_back(counts[key], key);
        }
    }
    std::sort(used.begin(), used.end(),
              [](const std::pair<int, int> &a, const std::pair<int, int> &b) {
                  return a.first > b.first;
              });

    const int paletteSize = qMin(256, static_cast<int>(used.size()));
    QVector<QRgb> palette;
    palette.reserve(paletteSize);
    for (int i = 0; i < paletteSize; ++i) {
        const int key = used[i].second;
        const int r = (key >> 10) & 0x1F;
        const int g = (key >> 5) & 0x1F;
        const int b = key & 0x1F;
        palette.append(qRgb((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)));
    }

    // 建立桶到调色板索引的映射：未进入调色板的颜色取最近色
    std::vector<quint8> indexMap(kColorBins, 0);
    for (int i = 0; i < paletteSize; ++i) {
        indexMap[used[i].second] = static_cast<quint8>(i);
    }
    for (size_t i = static_cast<size_t>(paletteSize); i < used.size(); ++i) {
        const int key = used[i].second;
        const int r = ((key >> 10) & 0x1F) * 255 / 31;
        const int g = ((key >> 5) & 0x1F) * 255 / 31;
        const int b = (key & 0x1F) * 255 / 31;
        int bestIndex = 0;
        int bestDist = std::numeric_limits<int>::max();
        for (int p = 0; p < paletteSize; ++p) {
            const QRgb c = palette.at(p);
            const int dr = qRed(c) - r;
            const int dg = qGreen(c) - g;
            const int db = qBlue(c) - b;
            const int dist = dr * dr + dg * dg + db * db;
            if (dist < bestDist) {
                bestDist = dist;
                bestIndex = p;
            }
        }
        indexMap[key] = static_cast<quint8>(bestIndex);
    }

    QImage indexed(width, height, QImage::Format_Indexed8);
    indexed.setColorTable(palette);
    for (int y = 0; y < height; ++y) {
        const QRgb *srcLine = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        uchar *dstLine = indexed.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const QRgb c = srcLine[x];
            const int key = ((qRed(c) >> 3) << 10) | ((qGreen(c) >> 3) << 5) | (qBlue(c) >> 3);
            dstLine[x] = indexMap[key];
        }
    }
    return indexed;
}

bool GifEncoder::addFrame(const QImage &frame, int delayCentiseconds) {
    if (!m_opened) {
        return false;
    }
    const QImage indexed = toIndexed(frame);
    if (indexed.isNull()) {
        return false;
    }
    const int delay = qBound(1, delayCentiseconds, 60000);

    if (m_hasPending && sameImage(indexed, m_pending)) {
        // 画面未变化：只延长上一帧的显示时间
        m_pendingDelay = qMin(60000, m_pendingDelay + delay);
        return true;
    }
    if (!flushPending()) {
        return false;
    }
    m_pending = indexed;
    m_pendingDelay = delay;
    m_hasPending = true;
    return true;
}

bool GifEncoder::flushPending() {
    if (!m_hasPending) {
        return true;
    }
    if (!writeImage(m_pending, m_pendingDelay)) {
        return false;
    }
    m_hasPending = false;
    m_pending = QImage();
    ++m_frameCount;
    return true;
}

bool GifEncoder::writeImage(const QImage &indexed, int delayCentiseconds) {
    const int width = indexed.width();
    const int height = indexed.height();
    const int colorCount = qBound(2, static_cast<int>(indexed.colorCount()), 256);

    int tableSize = 2;
    int sizeCode = 0;
    while (tableSize < colorCount) {
        tableSize <<= 1;
        ++sizeCode;
    }
    const int minCodeSize = qMax(2, sizeCode + 1);

    QByteArray block;
    auto appendU16 = [](QByteArray &buf, int value) {
        buf.append(static_cast<char>(value & 0xFF));
        buf.append(static_cast<char>((value >> 8) & 0xFF));
    };

    // 图形控制扩展：Disposal=1（保留），无透明色
    block.append(static_cast<char>(0x21));
    block.append(static_cast<char>(0xF9));
    block.append(static_cast<char>(0x04));
    block.append(static_cast<char>(0x04));
    appendU16(block, delayCentiseconds);
    block.append(static_cast<char>(0x00));
    block.append(static_cast<char>(0x00));

    // 图像描述符：整帧、使用局部色表
    block.append(static_cast<char>(0x2C));
    appendU16(block, 0);
    appendU16(block, 0);
    appendU16(block, width);
    appendU16(block, height);
    block.append(static_cast<char>(0x80 | sizeCode));

    // 局部色表（补齐到 2 的幂）
    for (int i = 0; i < tableSize; ++i) {
        const QRgb rgb = (i < colorCount) ? indexed.color(i) : qRgb(0, 0, 0);
        block.append(static_cast<char>(qRed(rgb)));
        block.append(static_cast<char>(qGreen(rgb)));
        block.append(static_cast<char>(qBlue(rgb)));
    }

    // 像素索引：逐行拼接（去掉行对齐填充）
    QByteArray indices;
    indices.reserve(width * height);
    for (int y = 0; y < height; ++y) {
        indices.append(reinterpret_cast<const char *>(indexed.constScanLine(y)), width);
    }

    block.append(static_cast<char>(minCodeSize));
    const QByteArray lzw = encodeLzw(indices, minCodeSize);
    if (lzw.isEmpty()) {
        return false;
    }
    block.append(lzw);

    return writeBytes(block);
}

QByteArray GifEncoder::encodeLzw(const QByteArray &indices, int minCodeSize) const {
    if (indices.isEmpty()) {
        return QByteArray();
    }

    const int clearCode = 1 << minCodeSize;
    const int endCode = clearCode + 1;
    int nextCode = endCode + 1;
    int codeSize = minCodeSize + 1;

    std::vector<HashEntry> table(kHashSize);

    quint32 bitBuffer = 0;
    int bitCount = 0;
    QByteArray compressed;
    compressed.reserve(indices.size() / 2 + 64);

    auto emitCode = [&](int code) {
        bitBuffer |= (static_cast<quint32>(code) << bitCount);
        bitCount += codeSize;
        while (bitCount >= 8) {
            compressed.append(static_cast<char>(bitBuffer & 0xFF));
            bitBuffer >>= 8;
            bitCount -= 8;
        }
        // 码宽提升必须在写出当前码之后进行（与 giflib/compress 一致）：
        // 解码器每读一个码才把字典推进一格，若提前加宽会导致后续所有码错位。
        if (nextCode >= (1 << codeSize) && codeSize < 12) {
            ++codeSize;
        }
    };

    emitCode(clearCode);

    int prefix = static_cast<unsigned char>(indices.at(0));
    for (int i = 1; i < indices.size(); ++i) {
        const int suffix = static_cast<unsigned char>(indices.at(i));
        const int key = (prefix << 8) | suffix;
        const int found = findCode(table, key);
        if (found >= 0) {
            prefix = found;
            continue;
        }

        emitCode(prefix);

        if (nextCode < LZ_MAX_CODE) {
            insertCode(table, key, nextCode);
            ++nextCode;
        } else {
            // 字典已满：发送清除码并重置（上限 4095 与解码器保持一致）
            emitCode(clearCode);
            resetTable(table);
            nextCode = endCode + 1;
            codeSize = minCodeSize + 1;
        }
        prefix = suffix;
    }

    emitCode(prefix);
    emitCode(endCode);
    if (bitCount > 0) {
        compressed.append(static_cast<char>(bitBuffer & 0xFF));
    }

    // 按 255 字节切分为 GIF 子块
    QByteArray blocks;
    blocks.reserve(compressed.size() + compressed.size() / 255 + 2);
    for (int pos = 0; pos < compressed.size(); pos += 255) {
        const int chunk = qMin(255, compressed.size() - pos);
        blocks.append(static_cast<char>(chunk));
        blocks.append(compressed.constData() + pos, chunk);
    }
    blocks.append(static_cast<char>(0x00));
    return blocks;
}

bool GifEncoder::close() {
    if (!m_opened) {
        return true;
    }
    if (!flushPending()) {
        abort();
        return false;
    }
    const bool ok = writeBytes(QByteArray(1, static_cast<char>(0x3B)));  // 文件结束符
    m_file.flush();
    m_file.close();
    m_opened = false;
    return ok;
}

void GifEncoder::abort() {
    if (m_file.isOpen()) {
        m_file.close();
    }
    if (!m_file.fileName().isEmpty()) {
        QFile::remove(m_file.fileName());
    }
    m_opened = false;
    m_hasPending = false;
    m_pending = QImage();
    m_pendingDelay = 0;
    m_frameCount = 0;
}

bool GifEncoder::writeBytes(const QByteArray &data) {
    return m_file.write(data) == data.size();
}
