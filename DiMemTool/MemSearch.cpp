#include "MemSearch.hpp"
#include "../diRW/baseRW.hpp"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <vector>
#include <algorithm>

namespace DiMemTool {

// ── 全局搜索对齐 ──
size_t g_memSearchAlign = 1;

// ═══════════════════════════════════════════
//  内部工具
// ═══════════════════════════════════════════

// 带通配符的字节比较
// N46: mask 比 size 短时立即停（视为通配），避免越界读 mask 触发 UB
static bool bytesMatch(const uint8_t* data, const uint8_t* pattern,
                       const char* mask, size_t size) {
    if (!mask) return memcmp(data, pattern, size) == 0;
    for (size_t i = 0; i < size; ++i) {
        if (mask[i] == '\0') return true;  // mask 截断：剩余位置按通配处理
        if (mask[i] == 'x' && data[i] != pattern[i])
            return false;
    }
    return true;
}

// 在本地缓冲区中查找第一个匹配位置 (memchr 锚点优化)
// 返回 (uintptr_t)-1 表示未找到，否则返回相对 buf 的偏移
static uintptr_t findInRange(const uint8_t* buf, size_t bufLen,
                              const uint8_t* pattern, const char* mask,
                              size_t patternSize) {
    static constexpr uintptr_t NOT_FOUND = (uintptr_t)-1;
    if (patternSize == 0 || bufLen < patternSize) return NOT_FOUND;

    const uint8_t* scanEnd = buf + bufLen - patternSize;

    if (!mask) {
        for (const uint8_t* p = buf; p <= scanEnd; ++p) {
            p = (const uint8_t*)memchr(p, pattern[0], (scanEnd - p) + 1);
            if (!p) break;
            if (memcmp(p, pattern, patternSize) == 0)
                return (uintptr_t)(p - buf);
        }
        return NOT_FOUND;
    }

    size_t anchorIdx = 0;
    while (anchorIdx < patternSize && mask[anchorIdx] != 'x')
        ++anchorIdx;
    // B2: 全通配符（无 anchor）—— 退化为逐字节匹配，返回 0 让调用方推进
    if (anchorIdx >= patternSize) return 0;

    uint8_t anchorByte = pattern[anchorIdx];
    const uint8_t* anchorStart = buf + anchorIdx;
    const uint8_t* anchorEnd = scanEnd + anchorIdx;

    for (const uint8_t* cur = anchorStart; cur <= anchorEnd; ++cur) {
        cur = (const uint8_t*)memchr(cur, anchorByte, (anchorEnd - cur) + 1);
        if (!cur) break;
        const uint8_t* candidate = cur - anchorIdx;
        if (bytesMatch(candidate, pattern, mask, patternSize))
            return (uintptr_t)(candidate - buf);
    }
    return NOT_FOUND;
}

// 去除字符串中所有空白字符 (空格 / Tab / 换行)
// B6: 用于把带空格的 mask 归一化，避免 "x x x x" 因长度不匹配静默失败
static std::string compactString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            out += c;
        }
    }
    return out;
}

// Hex 字符串 → 字节数组
// N45: 用 compactString 统一剥空格/Tab/换行，避免 "DE\nAD" 被解析成 [0xDE, 0x0A, 0xD]
// N42: 显式校验每个字符是合法 hex，避免 "G1 02" 静默变成 [0, 0x02]
static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::string s = compactString(hex);
    if (s.size() % 2 != 0) return {};  // B8: 奇数长度
    std::vector<uint8_t> bytes;
    bytes.reserve(s.size() / 2);
    for (size_t i = 0; i < s.size(); i += 2) {
        unsigned char c1 = (unsigned char)s[i], c2 = (unsigned char)s[i+1];
        if (!std::isxdigit(c1) || !std::isxdigit(c2)) return {};
        bytes.push_back((uint8_t)strtoul(s.substr(i, 2).c_str(), nullptr, 16));
    }
    return bytes;
}

// 取当前生效的对齐值。S23: g_memSearchAlign 是 extern 全局变量，
// 用户可能绕过 setSearchAlign 直接写 0；读点必须兜底防除零 UB
static size_t effectiveAlign() {
    size_t a = g_memSearchAlign;
    return a == 0 ? 1 : a;
}

// 在指定范围内逐页搜索，返回所有命中
// B10 已修：用 tail (patternSize-1 字节) + new chunk 的 overlap 读取，
// 跨页命中（起点在 [addr+chunk-patternSize, addr+chunk)）不再漏报
// 限制：patternSize > PAGE+4096 的极端 pattern 需要多页连续 read（暂不支持，直接 return 0）
static std::vector<MemSearchResult> searchRangeInPages(
    diRW::baseRW* rw, uintptr_t start, uintptr_t end,
    const uint8_t* pattern, size_t patternSize, const char* mask) {

    std::vector<MemSearchResult> results;
    const size_t PAGE = 4096;

    if (patternSize == 0) return results;

    if (patternSize > PAGE + 4096) return results;

    // B10: overlap buffer = 上次末尾 (patternSize-1) 字节 + 本次新读 chunk 字节
    std::vector<uint8_t> buf(PAGE + patternSize - 1);
    std::vector<uint8_t> tail(patternSize - 1, 0);
    size_t tailLen = 0;

    size_t align = effectiveAlign();

    // N37: 全通配符检测 —— mask 里没有任何 'x'，等效于"每字节都匹配"
    bool allWildcard = (mask != nullptr);
    for (size_t i = 0; allWildcard && i < patternSize; ++i) {
        if (mask[i] == 'x') allWildcard = false;
    }

    uintptr_t addr = start;
    while (addr < end) {
        size_t chunk = std::min(PAGE, (size_t)(end - addr));

        // 把上次的 tail 拼到 buf 开头
        if (tailLen > 0) {
            memcpy(buf.data(), tail.data(), tailLen);
        }

        // 读 chunk 字节到 buf[tailLen..tailLen+chunk-1]
        if (!rw->readv(addr, buf.data() + tailLen, chunk)) {
            addr += chunk;
            tailLen = 0;  // 读失败时清掉 tail，避免错位
            continue;
        }

        size_t searchLen = tailLen + chunk;
        uintptr_t baseAddr = addr - tailLen;  // buf[0] 对应的绝对地址

        if (allWildcard) {
            // N37 fast path: 按 stride 推进，不调 findInRange
            size_t stride = (align > 1) ? align : 1;
            size_t startOff = 0;
            if (align > 1) {
                uintptr_t mod = baseAddr % align;
                if (mod != 0) startOff = align - mod;
            }
            for (size_t off = startOff; off + patternSize <= searchLen; off += stride) {
                MemSearchResult r;
                r.address = baseAddr + off;
                r.data = std::make_unique<uint8_t[]>(patternSize);
                memcpy(r.data.get(), buf.data() + off, patternSize);
                r.size = patternSize;
                results.push_back(std::move(r));
            }
        } else {
            // findInRange 路径 + B10 overlap
            size_t scanOff = 0;
            while (scanOff + patternSize <= searchLen) {
                uintptr_t off = findInRange(buf.data() + scanOff, searchLen - scanOff,
                                             pattern, mask, patternSize);
                if (off == (uintptr_t)-1) break;

                uintptr_t foundAddr = baseAddr + scanOff + off;

                // 对齐跳过
                if (align > 1 && (foundAddr % align) != 0) {
                    size_t skip = align - (foundAddr % align);
                    scanOff += off + skip;
                    continue;
                }

                MemSearchResult r;
                r.address = foundAddr;
                r.data = std::make_unique<uint8_t[]>(patternSize);
                memcpy(r.data.get(), buf.data() + scanOff + off, patternSize);
                r.size = patternSize;
                results.push_back(std::move(r));

                scanOff += off + 1;
            }
        }


        size_t saveLen = (searchLen < patternSize - 1) ? searchLen : (patternSize - 1);
        if (saveLen > 0) {
            memcpy(tail.data(), buf.data() + searchLen - saveLen, saveLen);
            tailLen = saveLen;
        } else {
            tailLen = 0;
        }

        addr += chunk;
    }
    return results;
}

// ═══════════════════════════════════════════
//  独立搜索函数实现
// ═══════════════════════════════════════════

// ── 工具：把详细结果转为普通地址数组 ──

static std::vector<uintptr_t> resultsToAddresses(std::vector<MemSearchResult>&& results) {
    std::vector<uintptr_t> addrs;
    addrs.reserve(results.size());
    for (auto& r : results) {
        addrs.push_back(r.address);
    }
    return addrs;
}

// ── 普通版实现 ──

std::vector<uintptr_t> memFindBytes(
    diRW::baseRW* rw, const AddressRange& range,
    const void* bytes, size_t size, const char* mask) {
    return resultsToAddresses(memFindBytesEx(rw, range, bytes, size, mask));
}

std::vector<uintptr_t> memFindHex(
    diRW::baseRW* rw, const AddressRange& range,
    const std::string& hex, const std::string& mask) {
    return resultsToAddresses(memFindHexEx(rw, range, hex, mask));
}

std::vector<uintptr_t> memFindIdaPattern(
    diRW::baseRW* rw, const AddressRange& range,
    const std::string& pattern) {
    return resultsToAddresses(memFindIdaPatternEx(rw, range, pattern));
}

// MapsEntry 重载
std::vector<uintptr_t> memFindBytes(diRW::baseRW* rw, const MapsEntry& entry,
                                    const void* bytes, size_t size, const char* mask) {
    return resultsToAddresses(memFindBytesEx(rw, entry, bytes, size, mask));
}
std::vector<uintptr_t> memFindHex(diRW::baseRW* rw, const MapsEntry& entry,
                                  const std::string& hex, const std::string& mask) {
    return resultsToAddresses(memFindHexEx(rw, entry, hex, mask));
}
std::vector<uintptr_t> memFindIdaPattern(diRW::baseRW* rw, const MapsEntry& entry,
                                         const std::string& pattern) {
    return resultsToAddresses(memFindIdaPatternEx(rw, entry, pattern));
}

// MapsEntry 数组重载
std::vector<uintptr_t> memFindBytes(diRW::baseRW* rw, const std::vector<MapsEntry>& regions,
                                    const void* bytes, size_t size, const char* mask) {
    return resultsToAddresses(memFindBytesEx(rw, regions, bytes, size, mask));
}
std::vector<uintptr_t> memFindHex(diRW::baseRW* rw, const std::vector<MapsEntry>& regions,
                                  const std::string& hex, const std::string& mask) {
    return resultsToAddresses(memFindHexEx(rw, regions, hex, mask));
}
std::vector<uintptr_t> memFindIdaPattern(diRW::baseRW* rw, const std::vector<MapsEntry>& regions,
                                         const std::string& pattern) {
    return resultsToAddresses(memFindIdaPatternEx(rw, regions, pattern));
}

// ── 详细版（Ex）实现 ──

std::vector<MemSearchResult> memFindBytesEx(
    diRW::baseRW* rw, const AddressRange& range,
    const void* bytes, size_t size, const char* mask) {

    if (!rw || !bytes || size == 0 || range.begin >= range.end)
        return {};

    return searchRangeInPages(rw, range.begin, range.end,
                              (const uint8_t*)bytes, size, mask);
}

std::vector<MemSearchResult> memFindHexEx(
    diRW::baseRW* rw, const AddressRange& range,
    const std::string& hex, const std::string& mask) {

    if (!rw || hex.empty() || mask.empty())
        return {};

    auto bytes = hexToBytes(hex);
    // B6: mask 归一化后再比较长度，支持 "x x x x" 这类带空格的写法
    std::string maskCompact = compactString(mask);
    size_t scanSize = maskCompact.length();
    if (bytes.size() != scanSize) return {};

    return memFindBytesEx(rw, range, bytes.data(), scanSize, maskCompact.c_str());
}

std::vector<MemSearchResult> memFindIdaPatternEx(
    diRW::baseRW* rw, const AddressRange& range,
    const std::string& pattern) {

    std::vector<uint8_t> bytes;
    std::string mask;
    std::string tok;

    for (size_t i = 0; i <= pattern.size(); ++i) {
        char c = pattern[i];
        if (c == ' ' || c == '\0' || c == '\t') {
            if (!tok.empty()) {
                if (tok == "?") {
                    bytes.push_back(0);
                    mask += '?';
                } else if (tok.size() == 2
                           && std::isxdigit((unsigned char)tok[0])
                           && std::isxdigit((unsigned char)tok[1])) {
                    bytes.push_back((uint8_t)strtoul(tok.c_str(), nullptr, 16));
                    mask += 'x';
                } else {
                    // B7: 解析失败 (e.g. "???" / "G1" / 空心) 显式返回空
                    // 避免此前静默丢弃后产生比预期短的 pattern
                    return {};
                }
                tok.clear();
            }
        } else {
            tok += c;
        }
    }

    if (bytes.empty() || mask.empty() || bytes.size() != mask.size())
        return {};

    return memFindBytesEx(rw, range, bytes.data(), bytes.size(), mask.c_str());
}

// ── MapsEntry 重载 ──

std::vector<MemSearchResult> memFindBytesEx(
    diRW::baseRW* rw, const MapsEntry& entry,
    const void* bytes, size_t size, const char* mask) {
    return memFindBytesEx(rw, entry.range(), bytes, size, mask);
}

std::vector<MemSearchResult> memFindHexEx(
    diRW::baseRW* rw, const MapsEntry& entry,
    const std::string& hex, const std::string& mask) {
    return memFindHexEx(rw, entry.range(), hex, mask);
}

std::vector<MemSearchResult> memFindIdaPatternEx(
    diRW::baseRW* rw, const MapsEntry& entry,
    const std::string& pattern) {
    return memFindIdaPatternEx(rw, entry.range(), pattern);
}

// ── MapsEntry 数组重载 ──

std::vector<MemSearchResult> memFindBytesEx(
    diRW::baseRW* rw, const std::vector<MapsEntry>& regions,
    const void* bytes, size_t size, const char* mask) {
    std::vector<MemSearchResult> all;
    // B11: 粗略预分配 —— 按"每 64 字节 1 命中"估算，减少多段搜索时的反复 realloc
    size_t estimate = 0;
    for (const auto& e : regions) {
        if (e.isValid() && e.size() >= size) {
            estimate += e.size() / 64 + 1;
        }
    }
    all.reserve(estimate);
    for (const auto& e : regions) {
        auto r = memFindBytesEx(rw, e.range(), bytes, size, mask);
        all.insert(all.end(), std::make_move_iterator(r.begin()), std::make_move_iterator(r.end()));
    }
    return all;
}

std::vector<MemSearchResult> memFindHexEx(
    diRW::baseRW* rw, const std::vector<MapsEntry>& regions,
    const std::string& hex, const std::string& mask) {
    std::vector<MemSearchResult> all;
    size_t estimate = 0;
    for (const auto& e : regions) {
        estimate += e.size() / 64 + 1;
    }
    all.reserve(estimate);
    for (const auto& e : regions) {
        auto r = memFindHexEx(rw, e.range(), hex, mask);
        all.insert(all.end(), std::make_move_iterator(r.begin()), std::make_move_iterator(r.end()));
    }
    return all;
}

std::vector<MemSearchResult> memFindIdaPatternEx(
    diRW::baseRW* rw, const std::vector<MapsEntry>& regions,
    const std::string& pattern) {
    std::vector<MemSearchResult> all;
    size_t estimate = 0;
    for (const auto& e : regions) {
        estimate += e.size() / 64 + 1;
    }
    all.reserve(estimate);
    for (const auto& e : regions) {
        auto r = memFindIdaPatternEx(rw, e.range(), pattern);
        all.insert(all.end(), std::make_move_iterator(r.begin()), std::make_move_iterator(r.end()));
    }
    return all;
}

} // namespace DiMemTool
