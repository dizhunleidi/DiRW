# diRW

**Android 进程内存读写库（多后端架构）**

diRW 是一个 C++17 编写的 Android 进程内存读写库。通过统一的抽象接口提供了 **6 种后端实现**，切换后端无需改动业务代码。

---

## 更新日志

版本变更记录见 [CHANGELOG.md](CHANGELOG.md)。

---

## 特性

- **统一 API** — 所有后端继承自 `baseRW`，提供相同的 `readv` / `writev` 接口
- **6 种后端** — syscall、memcpy、QX11 驱动、RT 驱动、TGod 内核模块、TwT 驱动
- **连接状态** — 所有后端统一维护 `connected` 标志，对接失败不退出进程，用 `isConnected()` 查询
- **线程安全标志** — 全局 PID 与 TGod 读模式均为 `std::atomic`，多线程切换立即可见
- **PID 模式** — 全局模式（共享 PID）和私有模式（各自独立 PID）
- **读模式作用域** — `TGodRW` 的有缓存/无缓存读模式同样支持 Global/Private 双作用域
- **指针链** — 多级指针偏移追踪 `jumpPoint()`
- **丰富的数据读取** — Dword、Float、Bool、Ptr32、Ptr64、UTF-8 字符串
- **模块基址** — 解析目标进程共享库加载地址 `get_module_base()`
- **Header-Only** — 直接 `#include` 即可使用，无需单独编译
- **易扩展** — 实现三个虚函数即可接入自定义后端

---

## 环境要求

- Android NDK r17+（推荐 r20 或更高）
- 目标 API：**Android 8.0+ (API 26)**
- ABI：`arm64-v8a`
- Root 权限：跨进程读写其他应用时，`syscallRW` 与驱动后端（`Qx11RW`、`RtRW`、`TGodRW`、`TwTRW`）都需要 root；仅 `copyRW`（同进程）完全免 root

---

## 后端对比

| 后端 | Root | 内核驱动 | 作用范围 | 速度 | 说明 |
|------|:----:|:--------:|:--------:|:----:|------|
| **syscallRW** | 跨进程需要 | 不需要 | 跨进程/同进程 | 快 | 使用 `process_vm_readv`/`writev` 系统调用。同 UID 进程免 root，跨 UID（读其他应用）需要 root |
| **copyRW** | 不需要 | 不需要 | 同进程 | 最快 | 直接 `memcpy`，不能访问其他进程 |
| **Qx11RW** | 需要 | QX11 驱动 | 跨进程/同进程 | 快 | 通过 `ioctl` 与 QX11 内核驱动通信 |
| **RtRW** | 需要 | Rt 驱动 | 跨进程/同进程 | 快 | 通过 `ioctl` 与 RT 内核驱动通信 |
| **TGodRW** | 需要 | TGod 模块 (KPM) | 跨进程/同进程 | 快/中等 | 通过 inet socket 的 `ioctl` 与内核模块通信；支持无缓存读取 |
| **TwTRW** | 需要 | TwT 驱动 | 跨进程/同进程 | 快 | fd 由驱动的 reboot 魔数分支下发（anon_inode），`ioctl` 通信；附带触摸/陀螺仪/硬件断点 |

**推荐：** `syscallRW` 是最通用的后端，但跨 UID 读其他应用在 Android 4.5+ 同样需要 root；
读不到时换内核驱动后端（TGodRW/Qx11RW/RtRW/TwTRW）。

---

## 编译

### 方式一：ndk-build（默认）

```bash
# 在仓库根目录执行（仓库内已无 jni/ 嵌套，需显式指定构建脚本）
ndk-build NDK_PROJECT_PATH=. NDK_APPLICATION_MK=Application.mk APP_BUILD_SCRIPT=Android.mk
```

编译产物：`libs/arm64-v8a/diRW_test`

### 方式二：CMake（可选，与 ndk-build 等价）

```bash
# 在仓库根目录执行，$NDK 换成你的 NDK 路径
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

编译产物：`build/diRW_test`

Android Studio 项目里引用（`build.gradle` 模块级）：

```groovy
android {
    externalNativeBuild {
        cmake {
            path "jni/CMakeLists.txt"
            version "3.22.1"
        }
    }
    defaultConfig {
        ndk { abiFilters "arm64-v8a" }
        externalNativeBuild {
            cmake { arguments "-DANDROID_PLATFORM=android-26" }
        }
    }
}
```

---

## 使用示例

### 1. 导入头文件

```cpp
#include "syscallRW.hpp"   // 系统调用方式（免root）
// #include "copyRW.hpp"    // 同进程 memcpy
// #include "Qx11RW.hpp"    // QX11 内核驱动
// #include "RtRW.hpp"      // Rt 内核驱动
// #include "TGodRW.hpp"    // TGod 内核模块（KPM）
// #include "TwTRW.hpp"     // TwT 内核驱动

using namespace diRW;
```

### 2. 创建实例与连接检查

```cpp
// 私有模式 — 每个实例独立 PID
auto* rw = new syscallRW(baseRW::PidMode::Private, getPID("com.example.app"));

// 驱动后端对接失败时构造函数不会退出进程，用 isConnected() 检查
if (!rw->isConnected()) {
    // 换备用后端，或提示用户加载驱动
}
```

### 3. 读写内存

```cpp
// 读取
int val       = rw->getDword(0x12345678);          // 4 字节整数
float f       = rw->getFloat(0x12345678);           // 4 字节浮点数
bool b        = rw->getBool(0x12345678);            // 1 字节布尔
uintptr_t p64 = rw->getPtr64(0x12345678);           // 8 字节指针（64位）
uintptr_t p32 = rw->getPtr32(0x12345678);           // 4 字节指针（32位）
char* str     = rw->getUTF8(0x12345678);            // UTF-16 → UTF-8 字符串（最多14字符）

// 写入
rw->writeFloat(0x12345678, 3.14f);

// 原始读写（自定义结构体/数组）
int buf[4];
rw->readv(0x12345678, buf, sizeof(buf));
rw->writev(0x12345678, buf, sizeof(buf));
```

### 4. 指针链

```cpp
// 多级指针追踪：
//   [base + offset0] → ptr1 → [ptr1 + offset1] → ptr2 → ... + finalOffset
// 等价于: *(*(*(base + 0x1A4) + 0x2B8) + 0x3C0) + 0x10
uintptr_t result = rw->jumpPoint(baseAddr, 0x1A4, 0x2B8, 0x3C0) + 0x10;
```

### 5. 获取模块基址

```cpp
uintptr_t base = rw->get_module_base("libil2cpp.so");
if (base != 0) {
    // 目标地址 = base + 偏移
    int value = rw->getDword(base + 0x123456);
}
```

### 6. PID 模式说明

```cpp
// 私有模式：实例之间 PID 互不干扰
auto* a = new syscallRW(baseRW::PidMode::Private, getPID("app1"));
auto* b = new syscallRW(baseRW::PidMode::Private, getPID("app2"));

// 全局模式：所有实例共享一个 PID
baseRW::setGlobalPid(getPID("app1"));
// 所有全局模式实例现在都读写 "app1"
baseRW::setGlobalPid(getPID("app2"));
// 所有全局模式实例自动切换到 "app2"
```

### 7. TGodRW 读模式（有缓存/无缓存）

```cpp
// 读模式的作用域复用 PID 的 Global/Private 语义（构造第三个参数，默认 Global）
auto* rw = new TGodRW(baseRW::PidMode::Private, getPID("com.example.app"));

// Global 模式（默认）：对所有 Global 实例生效
rw->setMod(TGodRW::ReadMode::NO_CACHE);
rw->setMod(TGodRW::ReadMode::NORMAL);

// 显式操作全局标志
TGodRW::setGlobalMod(TGodRW::ReadMode::NO_CACHE);
TGodRW::ReadMode m = TGodRW::getGlobalMod();

// 多线程场景：每线程一个 Private 读模式的实例，互不干扰
auto* rw2 = new TGodRW(baseRW::PidMode::Private, pid, baseRW::PidMode::Private);
rw2->setMod(TGodRW::ReadMode::NO_CACHE);   // 只影响 rw2
```

无缓存读取由内核模块走页表 + `vmap` 实现，绕过 CPU cache，适合读取易变数据，但比普通读取慢。

### 8. TwTRW 驱动附带功能

```cpp
// TwT 驱动的 fd 由 reboot 系统调用魔数分支自动下发，构造即完成对接
// 第 4/5 个参数可独立启用陀螺仪和触摸（-1 不启用，默认）：
//   gyro_mode: 0=tracepoint 1=uprobe    touch_mode: 0/1
// 第 3 个参数是读模式作用域（同 TGodRW，默认 Global）
auto* rw = new TwTRW(baseRW::PidMode::Private, getPID("com.example.app"), baseRW::PidMode::Global, 0, 1);
if (!rw->isConnected()) { /* 驱动未加载或无 root */ }

// 也可以构造后单独初始化（与构造参数互不影响，各自独立）
rw->gyro_init(0);
rw->touch_init(1);

// 读模式（同 TGodRW 的作用域处理，复用 pid 的 Global/Private 语义）：
//   MOD1（默认）走 READ_MEM；MOD2 走 READ_MEM_V2 —— 驱动的两条读取通道
rw->setMod(TwTRW::ReadMode::MOD2);       // Global 实例改全局，Private 实例只改自己
rw->setMod(TwTRW::ReadMode::MOD1);

// 读写与其他后端一致（readv/writev/getDword/getFloat/...）
int v = 0;
rw->readv(addr, &v, sizeof(v));

// 驱动侧能力
pid_t pid = rw->get_pid_by_name("com.example.app");  // 按进程名取 PID
uintptr_t bss = rw->get_module_bss("libil2cpp.so");  // 模块 .bss 基址

// 触摸注入（模式 0/1，先 init）
rw->touch_init(0);
rw->touch_down(0, 500, 800);
rw->touch_up(0);

// 陀螺仪（0=tracepoint 1=uprobe，先 init）
rw->gyro_init(0);
rw->gyro_modify(1.5f, -0.5f);

// 硬件断点：命中时读寄存器快照
rw->bp_init_driver();
uint64_t handle = rw->bp_inst(pid, addr, TWT_BP_LEN_4, TWT_BP_RW);
auto hits = rw->bp_get_hits(handle);                 // std::vector<TwtHitItem>
for (auto& h : hits) {
    uint64_t x0 = TwTRW::get_xregs(h, 0);            // X0 寄存器
    float s0 = TwTRW::get_vregs_float(h, 0);         // V0 浮点寄存器
}
rw->bp_uninst(handle);
```

断点还支持命中瞬间自动修改寄存器（`bp_inst` 传 `TwtRegBatch`，或事后 `bp_set_reg`/`bp_set_pc`/`bp_set_vreg_float` 等），详见 `TwTRW.hpp`。

### 9. 工具函数（tool.h）

```cpp
int pid = getPID("com.example.app");                    // 包名 → PID
uintptr_t base = get_module_base(pid, "libc.so");       // PID + 模块名 → 基址
```

---

## 添加自定义后端

diRW 设计为易于扩展。只要继承 `baseRW` 并实现三个纯虚函数，就能接入你自己的内存读写方式。

### 步骤

1. 创建一个新的 `.hpp` 文件，继承 `baseRW`
2. 构造成功时把继承来的 `connected` 置为 `true`
3. 实现以下三个虚函数：

| 纯虚函数 | 说明 |
|---------|------|
| `bool readv(uintptr_t addr, void* buf, size_t size)` | 从目标进程 `addr` 读取 `size` 字节到 `buf` |
| `bool writev(uintptr_t addr, void* buf, size_t size)` | 向目标进程 `addr` 写入 `size` 字节 |
| `uintptr_t get_module_base(const char* name)` | 返回目标进程中 `name` 模块的加载基址 |

### 完整示例

下面是一个真实的自定义后端模板：

```cpp
// File: jni/diRW/myCustomRW.hpp
#include "baseRW.hpp"
#include <unistd.h>
#include <sys/fcntl.h>

namespace diRW {

class myCustomRW : public baseRW {
public:
    // 构造时传入目标PID
    myCustomRW(PidMode mode, int tpid = 0) : baseRW(mode, tpid) {
        // 在这里初始化你的驱动连接或资源
        fd = open("/dev/your_driver", O_RDWR);
        if (fd > 0)
            connected = true;   // 对接失败不退出进程，置好状态让调用方查询
    }

    ~myCustomRW() override {
        if (fd > 0) close(fd);
    }

    // 1. 内存读取
    bool readv(uintptr_t addr, void* buffer, size_t size) override {
        if (!connected)
            return false;
        YourStruct s { getPid(), addr, buffer, size };
        return ioctl(fd, YOUR_READ_CMD, &s) == 0;
    }

    // 2. 内存写入
    bool writev(uintptr_t addr, void* buffer, size_t size) override {
        if (!connected)
            return false;
        YourStruct s { getPid(), addr, buffer, size };
        return ioctl(fd, YOUR_WRITE_CMD, &s) == 0;
    }

    // 3. 获取模块基址
    uintptr_t get_module_base(const char* name) override {
        // 方式A：通过驱动获取
        // ModuleInfo m { getPid(), name };
        // ioctl(fd, YOUR_MODULE_CMD, &m);
        // return m.base;

        // 方式B：直接解析 /proc/<pid>/maps（参考 TGodRW::get_module_base 的实现）
        ...
    }

private:
    int fd = -1;
};

} // namespace diRW
```

### 使用自定义后端

```cpp
#include "myCustomRW.hpp"
using namespace diRW;

auto* rw = new myCustomRW(baseRW::PidMode::Private, getPID("com.example.app"));
int val = rw->getDword(0x12345678);
```

### 扩展时需要注意

- **`getPid()`** — 在子类中用这个 protected 方法获取当前 PID，不要直接访问 PID 成员
- **`connected`** — 继承来的 protected 成员，默认 `false`；构造成功时置 `true`，调用方通过 `isConnected()` 查询。不要在对接失败时 `exit()` 进程
- **模块基址** — 基类不提供 `/proc/pid/maps` 解析，驱动不支持取基址的话参考 `TGodRW::get_module_base` 自己解析
- **构造函数** — 必须透传 `PidMode` 和 `tpid` 给 `baseRW(mode, tpid)`
- **资源释放** — 在析构函数中关闭驱动句柄等资源
- **线程安全** — 若后端有类级全局标志（如读模式），用 `std::atomic` 并配 `relaxed` 序，参考 `TGodRW::globalMod`

---

## API 参考

### baseRW（抽象基类）

| 方法 | 返回 | 说明 |
|------|------|------|
| `isConnected()` | `bool` | 后端就绪状态（构造时确定，失败不退出进程） |
| `readv(addr, buf, size)` | `bool` | 原始内存读取（纯虚函数） |
| `writev(addr, buf, size)` | `bool` | 原始内存写入（纯虚函数） |
| `get_module_base(name)` | `uintptr_t` | 获取模块加载基址（纯虚函数） |
| `getDword(addr)` | `int` | 读取 4 字节整数 |
| `getFloat(addr)` | `float` | 读取 4 字节浮点数 |
| `getBool(addr)` | `bool` | 读取 1 字节布尔值 |
| `getPtr64(addr)` | `uintptr_t` | 读取 8 字节指针 |
| `getPtr32(addr)` | `uintptr_t` | 读取 4 字节指针 |
| `getUTF8(addr)` | `char*` | 读取 UTF-16 字符串（转 UTF-8，最多 14 字符） |
| `writeFloat(addr, val)` | `bool` | 写入 4 字节浮点数 |
| `jumpPoint(base, offsets...)` | `uintptr_t` | 多级指针链追踪 |
| `getProcessPid()` | `int` | 获取当前实例的目标 PID |
| `getGlobalPid()` | `int`（静态） | 获取全局 PID |
| `setGlobalPid(pid)` | `void`（静态） | 设置全局 PID |

### 基类提供的 protected 成员（供子类使用）

| 成员/方法 | 类型 | 说明 |
|------|------|------|
| `connected` | `bool` | 就绪状态标志，构造成功时置 `true` |
| `getPid()` | `int` | 获取当前 PID（自动处理全局/私有模式） |

### TGodRW 专属

| 方法 | 返回 | 说明 |
|------|------|------|
| `setMod(mode)` | `void` | 切换读模式（Global 实例改全局，Private 实例改自身） |
| `getMod()` | `ReadMode` | 本实例当前生效的读模式 |
| `setGlobalMod(mode)` | `void`（静态） | 设置全局读模式 |
| `getGlobalMod()` | `ReadMode`（静态） | 获取全局读模式 |

### TwTRW 专属

| 方法 | 返回 | 说明 |
|------|------|------|
| `setMod(mode)` | `void` | 切换读模式（Global 实例改全局，Private 实例改自身） |
| `getMod()` | `ReadMode` | 本实例当前生效的读模式 |
| `setGlobalMod(mode)` | `void`（静态） | 设置全局读模式 |
| `getGlobalMod()` | `ReadMode`（静态） | 获取全局读模式 |
| `get_pid_by_name(name)` | `pid_t` | 驱动侧按进程名取 PID |
| `get_module_bss(name)` | `uintptr_t` | 驱动侧取模块 .bss 基址 |
| `touch_init/down/up` | `bool` | 触摸注入 |
| `gyro_init/modify/disable` | `bool` | 陀螺仪控制 |
| `bp_inst/bp_uninst/...` | 见头文件 | 硬件断点/观察点全生命周期管理 |

构造函数第 4/5 个参数 `gyro_mode`、`touch_mode` 可在构造时独立初始化陀螺仪与
触摸（`-1` 不启用），也可构造后各自单独调用 `gyro_init()`/`touch_init()`。

**TwTRW 读模式**：作用域处理与 `TGodRW` 相同——`MOD1`（默认）走 `READ_MEM`、
`MOD2` 走 `READ_MEM_V2`，是驱动的两条读取通道；读模式作用域复用 PID 的
Global/Private 语义（构造第三个参数），支持全局切换（`setGlobalMod`）或按实例
独立，多线程互不干扰。

---

## 项目结构

```
DiRW/                        # 仓库根（原 jni/ 目录）
├── Android.mk               # NDK 构建配置（ndk-build）
├── CMakeLists.txt           # CMake 构建配置（可选，与 Android.mk 等价）
├── Application.mk           # ABI/平台设置
├── example.cpp              # 完整示例程序
├── tool.h                   # PID / 模块基址工具函数
├── README.md
└── diRW/
    ├── baseRW.hpp           # 抽象基类（PID 模式、连接状态、类型读写、指针链）
    ├── syscallRW.hpp        # 后端：process_vm 系统调用
    ├── copyRW.hpp           # 后端：memcpy 同进程读写
    ├── Qx11RW.hpp           # 后端：QX11 内核驱动
    ├── RtRW.hpp             # 后端：Rt 内核驱动
    ├── TGodRW.hpp           # 后端：TGod 内核模块（DiDevice.kpm）
    └── TwTRW.hpp            # 后端：TwT 内核驱动（anon_inode ioctl）
```

---

## License

[MIT](LICENSE)
