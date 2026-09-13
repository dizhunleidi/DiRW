#ifndef DIMEMTOOL_PAGETOOL_H
#define DIMEMTOOL_PAGETOOL_H

#include "MemoryCore.hpp"

#include <string>
#include <vector>

namespace DiMemTool {

// ── 纯工具函数（无状态）──

// 将 "/proc/pid/maps" 中的权限字段（如 "rw-p"）解析为 MemPerms 枚举
MemPerms parsePerms(const char* flags);

// 按 path + 权限字符串判定内存类型
MemType classify(const char* path, const char* flags);

// 按 path + 权限枚举判定内存类型
MemType classify(const char* path, MemPerms perms);

// ── PageTool 类：一次解析，缓存复用 ──

class PageTool {
public:
    // 构造时传入 pid，自动解析 maps
    explicit PageTool(int pid);

    // 重新读取 /proc/pid/maps 更新缓存
    bool refresh();

    // ── 路径名过滤查询（全部走缓存）──

    // 返回所有 maps 条目
    std::vector<MapsEntry> getMaps() const;

    // pathname 完整匹配
    std::vector<MapsEntry> getMapsEqual(const std::string& name) const;

    // pathname 包含子串
    std::vector<MapsEntry> getMapsContaining(const std::string& substr) const;

    // pathname 以指定前缀开头
    std::vector<MapsEntry> getMapsStartingWith(const std::string& prefix) const;

    // pathname 以指定后缀结尾
    std::vector<MapsEntry> getMapsEndingWith(const std::string& suffix) const;

    // pathname 匹配 POSIX 扩展正则表达式
    std::vector<MapsEntry> getMapsByRegex(const std::string& pattern) const;

    // ── 其他过滤查询 ──

    // 按 MemType 获取内存段
    std::vector<MapsEntry> getRegions(MemType type) const;

    // 按权限掩码获取内存段
    std::vector<MapsEntry> getRegionsByPerms(MemPerms perms) const;

    // 查指定地址落在哪个段（二分查找）
    MapsEntry getAddressMap(uintptr_t addr) const;

    // ── 访问器 ──
    int pid() const { return _pid; }
    const std::vector<MapsEntry>& entries() const { return _entries; }

private:
    int _pid;
    std::vector<MapsEntry> _entries;
};

} // namespace DiMemTool

#endif
