# DiMemTool — 内存工具集

基于 `diRW` 读写后端的 Android 进程内存拓展工具集，提供：

- 内存段查询（解析 `/proc/pid/maps` 并缓存）
- 进程 PID 查询
- 内存搜索（字节模式 + 分轮次值搜索）
- 内存补丁（备份→修改→恢复）

## 模块结构

```
DiMemTool/
├── MemoryCore.hpp        — 核心数据类型（AddressRange、MapsEntry、枚举等）
├── PageTool.hpp/cpp      — 内存段查询工具（/proc/pid/maps 解析与缓存）
├── ProcessTool.hpp/cpp   — 进程查询工具
├── MemSearch.hpp/cpp     — 内存搜索（独立函数 + MemSearch 类）
└── MemPatch.hpp/cpp      — 内存补丁
```

## 快速开始

```cpp
#include "DiMemTool/PageTool.hpp"
#include "DiMemTool/ProcessTool.hpp"
#include "DiMemTool/MemSearch.hpp"
#include "DiMemTool/MemPatch.hpp"
#include "diRW/syscallRW.hpp"

using namespace DiMemTool;

// 1. 查 PID
int pid = getPID("com.example.app");

// 2. 构造 PageTool，自动解析并缓存 maps
PageTool pt(pid);

// 3. 构造读写后端
diRW::syscallRW rw(diRW::baseRW::PidMode::Private, pid);

// 4. 查询内存段
auto all  = pt.getMaps();
auto libs = pt.getMapsEndingWith(".so");
auto rwRegions = pt.getRegionsByPerms(MemPerms::RW);

// 合并多个类型的段
auto regions = pt.getRegions(MemType::A);
regions += pt.getRegions(MemType::Ca);

// 5. 独立模式搜索
auto found = memFindBytes(&rw, rwRegions[0].range(), "\x00\x00\x00\x00", 4);

// 6. 值搜索（分轮次）
MemSearch ms(&rw);
int32_t target = 100;
ms.searchExact(rwRegions, &target, sizeof(target));  // 首搜
ms.nextBiggerThan(&target, sizeof(target));           // 精搜：>100
ms.nextIncreased();                                    // 精搜：值增大
ms.writeResult(0, &newVal, sizeof(newVal));           // 写入

// 7. 内存补丁
uint32_t patch = 0xDEADBEEF;
MemPatch mp = MemPatch::createWithBytes(&rw, addr, &patch, sizeof(patch));
mp.apply();    // 写入补丁
mp.restore();  // 恢复原始值
```

---

## 数据类型

### `AddressRange`

轻量地址范围。

```cpp
struct AddressRange {
    uintptr_t begin;
    uintptr_t end;
    size_t size() const;          // end - begin
};
```

### `MapsEntry`

完整的 `/proc/pid/maps` 条目。

```cpp
struct MapsEntry {
    uintptr_t start, end;
    MemPerms  perms;              // 权限位掩码
    uintptr_t offset;
    unsigned long dev_major, dev_minor;
    unsigned long inode;
    char path[256];

    bool isValid() const;         // start && end && end > start
    bool isUnknown() const;       // path 为空（匿名映射）

    bool is_private() const;
    bool is_shared() const;
    bool is_ro() const;
    bool is_rw() const;
    bool is_rx() const;

    bool contains(uintptr_t addr) const;   // addr ∈ [start, end)
    AddressRange range() const;            // → AddressRange{start, end}
    size_t size() const;                   // end - start
};
```

### 枚举

```cpp
enum class MemType {          // 内存区域分类
    A, Xa, As, B, Xs,        // A=匿名RW, Xa=App代码, As=Ashmem, B=字体, Xs=系统框架
    Ca, Cb, Cd,               // Ca=malloc, Cb=BSS, Cd=App库
    J, S, V, O                // J=Dalvik, S=栈, V=GPU, O=其他
};

enum class MemPerms : uint8_t {  // 权限位掩码
    None, Read, Write, Exec,
    Private, Shared,
    RW  = Read | Write,
    RWX = Read | Write | Exec,
};
```

### `MemSearchResult`

搜索结果结构体。`data` 由 `std::unique_ptr` 自动管理，无需手动释放。

```cpp
struct MemSearchResult {
    uintptr_t                 address;
    std::unique_ptr<uint8_t[]> data;   // 原始字节，自动管理
    size_t                    size;    // 字节数
};
```

**注意**：`MemSearchResult` 因包含 `unique_ptr` 而禁止拷贝，只可移动。Ex 系列搜索函数返回 `vector<MemSearchResult>` 时通过移动语义传递，不拷贝数据。

### 工具函数

```cpp
// 获取内存段总大小（字节）
size_t rangeSize(const AddressRange& r);
size_t rangeSize(const MapsEntry& e);
size_t rangeSize(const std::vector<AddressRange>& ranges);
size_t rangeSize(const std::vector<MapsEntry>& entries);

// 权限字符串解析
MemPerms parsePerms(const char* flags);   // "rw-p" → MemPerms

// 内存区域分类
MemType classify(const char* path, const char* flags);
MemType classify(const char* path, MemPerms perms);
```

---

## PageTool API

构造时自动解析 `/proc/pid/maps` 并缓存，所有查询走缓存，不重复读文件。

### 构造 & 刷新

| 方法 | 说明 |
|------|------|
| `PageTool(int pid)` | 构造并解析 maps |
| `bool refresh()` | 重新读取 maps 更新缓存 |
| `int pid() const` | 返回目标 PID |
| `const auto& entries() const` | 获取缓存的 maps 原始引用 |

### 路径名过滤（5 种模式）

| 方法 | 匹配规则 |
|------|---------|
| `getMaps()` | 返回全部 |
| `getMapsEqual(name)` | `pathname == name` |
| `getMapsContaining(substr)` | `pathname` 包含子串 |
| `getMapsStartingWith(prefix)` | `pathname` 以指定前缀开头 |
| `getMapsEndingWith(suffix)` | `pathname` 以指定后缀结尾 |
| `getMapsByRegex(pattern)` | POSIX 扩展正则匹配 |

### 权限/类型过滤

| 方法 | 返回 |
|------|------|
| `getRegions(MemType)` | 按内存类型过滤；支持 `operator+=` 合并多个类型 |
| `getRegionsByPerms(MemPerms)` | 按权限掩码过滤 |
| `getAddressMap(uintptr_t)` | 查指定地址落在哪个段（二分查找） |

---

## ProcessTool API

```cpp
int getPID(const char* packageName);  // 通过包名获取 PID，未找到返回 -1
```

---

## MemSearch API

### 搜索对齐

```cpp
extern size_t g_memSearchAlign;        // 全局变量，默认 1（逐字节扫）
void setSearchAlign(size_t align);    // 设置对齐值，0 退化为 1
```

| align | 行为 |
|-------|------|
| 1 | 逐字节扫（默认） |
| 4 | 只扫 4 字节对齐地址 |
| 8 | 只扫 8 字节对齐地址 |

影响所有独立搜索函数和 `MemSearch` 类的扫描操作。**非线程安全**，多线程场景由调用者同步。

### 独立搜索函数（无状态）

传入 `baseRW*` + 范围 + 数据，返回结果。每个模式分**普通版**（返回 `vector<uintptr_t>`）和**详细版**（返回 `vector<MemSearchResult>`，函数名加 `Ex`）。

| 模式 | 普通版 | 详细版 |
|------|--------|--------|
| 字节 + mask 通配符 | `memFindBytes(rw, range, bytes, size, mask?)` | `memFindBytesEx(...)` |
| Hex 字符串 | `memFindHex(rw, range, hex, mask)` | `memFindHexEx(...)` |
| IDA 风格模式 | `memFindIdaPattern(rw, range, pattern)` | `memFindIdaPatternEx(...)` |

每种函数有 3 种范围重载：`AddressRange` / `MapsEntry` / `vector<MapsEntry>`。

**Mask 规则**：
- `mask="xxxx"`：精确匹配
- `mask="x?xx"`：第 2 字节通配
- `mask="????"`：全通配
- `mask=nullptr`：全精确（默认）

**Hex 字符串规则**：
- 空格/Tab/换行均被忽略
- 字符必须为合法 hex 字符，否则返回空
- 长度必须为偶数

**IDA 模式规则**：
- 空格分隔
- `?` 表示通配，其他必须是 2 位 hex 字符
- 非法 token 整个搜索返回空

### MemSearch 类（有状态，分轮次）

构造时传入 `diRW::baseRW*`，内部管理结果列表，支持首搜→精搜流程。

```cpp
MemSearch(diRW::baseRW* rw);
~MemSearch();
```

**首搜**

| 方法 | 说明 |
|------|------|
| `searchExact(regions, value, valueSize)` | 精确匹配 |
| `searchRange(regions, minVal, maxVal, valueSize)` | [min, max] 区间 |
| `searchBiggerThan(regions, value, valueSize)` | 大于 |
| `searchSmallerThan(regions, value, valueSize)` | 小于 |

**精搜**（必须先进行首搜，且 `valueSize` 必须与首搜一致）

| 方法 | 说明 |
|------|------|
| `nextExact(value, valueSize)` | 等于给定值 |
| `nextRange(minVal, maxVal, valueSize)` | 在新区间内 |
| `nextBiggerThan(value, valueSize)` | 当前值 > 给定值 |
| `nextSmallerThan(value, valueSize)` | 当前值 < 给定值 |
| `nextIncreased()` | 比上次快照增大 |
| `nextDecreased()` | 比上次快照减小 |
| `nextChanged()` | 比上次快照变化 |
| `nextUnchanged()` | 比上次快照未变 |

**结果管理 & 写入**

| 方法 | 说明 |
|------|------|
| `results()` | 返回 `const vector<MemSearchResult>&` |
| `clear()` | 清空并释放所有结果 data |
| `writeResult(index, value, valueSize)` | 写入指定索引的结果 |
| `writeAddress(addr, value, valueSize)` | 写入任意地址 |

**比较规则**

| 值大小 | 处理方式 |
|--------|---------|
| 1/2/4/8 字节 | uint64 无符号整数比较 |
| 其他 | `memcmp` 字典序 |

**已知限制**：
- `patternSize` / `valueSize` 超过 8192 字节的搜索不支持
- 读失败的结果会被静默丢弃（不重试）
- `valueSize=0` / `value=nullptr` 直接返回 0 条
- `minVal > maxVal` 直接返回 0 条

---

## MemPatch API

备份→修改→恢复的内存补丁。

```cpp
MemPatch();
MemPatch(diRW::baseRW* rw, uintptr_t address,
         const void* patchCode, size_t patchSize);

bool isValid() const;
size_t size() const;
uintptr_t address() const;

bool apply();     // 写入补丁
bool restore();   // 恢复原始值

std::string currentBytes() const;   // 当前 hex
std::string originalBytes() const;  // 原始 hex
std::string patchBytes() const;     // 补丁 hex

// 工厂方法
static MemPatch createWithBytes(rw, addr, code, size);
static MemPatch createWithHex(rw, addr, hex);
```

构造时自动读取并备份原始字节，`apply()` 写入补丁，`restore()` 写回原始值。

---

## 构建

```bash
cmake -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/android-ndk/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-23

cmake --build build -j$(nproc)
```

编译单元：
- `DiMemTool/PageTool.cpp`
- `DiMemTool/ProcessTool.cpp`
- `DiMemTool/MemSearch.cpp`
- `DiMemTool/MemPatch.cpp`
