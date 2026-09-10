/*
 * diRW — Android 进程内存读写库（多后端架构）
 *
 * 本示例演示所有可用后端和功能。
 * 在已 root 的 Android 设备/模拟器上运行测试。
 *
 * 后端列表（根据环境选择）：
 *   1. syscallRW — process_vm readv/writev 系统调用（Android 6+，免 root）
 *   2. Qx11RW    — QX11 内核驱动 via ioctl（需要 root + 驱动）
 *   3. RtRW      — Root 级内核驱动 via ioctl（需要 root + 驱动）
 *   4. TGodRW    — TGod(DiDevice) 内核模块 via inet_ioctl（需要 root + KPM 模块）
 *   5. TwTRW     — TwT 内核驱动 via anon_inode ioctl（需要 root + 驱动）
 *   6. copyRW    — 直接 memcpy（仅同进程，免 root）
 */

#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <thread>
#include "syscallRW.hpp"
#include "Qx11RW.hpp"
#include "RtRW.hpp"
#include "TGodRW.hpp"
#include "TwTRW.hpp"
#include "copyRW.hpp"
#include "tool.h"

using namespace diRW;
using std::cout;
using std::endl;
using std::hex;
using std::dec;

// ============================================================
//  辅助宏：安全释放后端实例
// ============================================================
#define AUTO_DELETE(ptr) do { delete (ptr); (ptr) = nullptr; } while(0)

// ============================================================
//  1. 后端选择
// ============================================================
static void demo_backend_selection() {
    cout << "\n========== 1. 后端选择 ==========" << endl;

    /*
     * syscallRW — 最常用的后端
     * 使用 Android 的 process_vm readv/writev 系统调用
     * 同 UID 进程免 root；跨 UID 读其他应用需要 root
     */
    auto* rw = new syscallRW(baseRW::PidMode::Private, getPID("com.example.target"));
    cout << "[syscallRW]  PID: " << rw->getProcessPid() << endl;
    AUTO_DELETE(rw);

    /*
     * copyRW — 同进程内部读写，不需要 PID
     * 通过 memcpy 读写当前进程内存
     * PID 自动设为 getpid()
     */
    auto* local = new copyRW();
    cout << "[copyRW]    PID: " << local->getProcessPid() << " (自身进程)" << endl;
    AUTO_DELETE(local);

    /*
     * syscallRW(跨UID) / Qx11RW / RtRW / TGodRW / TwTRW — 需要 root
     * 如果驱动未加载，构造时会打印错误信息并继续运行
     */
    auto* qx = new Qx11RW(baseRW::PidMode::Private, getPID("com.example.target"));
    cout << "[Qx11RW]    PID: " << qx->getProcessPid() << endl;
    AUTO_DELETE(qx);

    cout << "===========================================" << endl;
}

// ============================================================
//  2. 全局模式 vs 私有模式
// ============================================================
static void demo_pid_mode() {
    cout << "\n========== 2. PID 模式：全局 vs 私有 ==========" << endl;

    /*
     * 私有模式（默认）：
     *   每个实例拥有独立的 PID，互不干扰
     *   PID 在构造时指定且不能更改
     */
    auto* priv1 = new syscallRW(baseRW::PidMode::Private, getPID("com.example.app1"));
    auto* priv2 = new syscallRW(baseRW::PidMode::Private, getPID("com.example.app2"));
    cout << "[Private]  rw1 PID: " << priv1->getProcessPid() << endl;
    cout << "[Private]  rw2 PID: " << priv2->getProcessPid() << endl;
    AUTO_DELETE(priv1);
    AUTO_DELETE(priv2);

    /*
     * 全局模式：
     *   所有实例共享一个全局 PID
     *   随时通过 setGlobalPid() 修改，所有实例自动生效
     */
    auto* g1 = new syscallRW(baseRW::PidMode::Global, getPID("com.example.app1"));
    cout << "[Global]   g1 PID: " << g1->getProcessPid() << endl;

    // 修改全局 PID — g1 自动切换到新目标
    baseRW::setGlobalPid(getPID("com.example.app2"));
    cout << "[Global]   调用 setGlobalPid 后 -> g1 PID: " << g1->getProcessPid() << endl;

    // 新全局实例直接取当前全局 PID
    auto* g2 = new syscallRW(baseRW::PidMode::Global);
    cout << "[Global]   g2 PID（不传参）: " << g2->getProcessPid() << endl;

    auto* g3 = new syscallRW(baseRW::PidMode::Global, getPID("com.example.app3"));
    cout << "[Global]   g3 PID: " << g3->getProcessPid() << endl;
    // 现在 globalPid == app3 的 PID，g1 和 g2 也都跟着变了
    cout << "[Global]   g1 现在指向: " << g1->getProcessPid() << endl;
    cout << "[Global]   g2 现在指向: " << g2->getProcessPid() << endl;

    AUTO_DELETE(g1);
    AUTO_DELETE(g2);
    AUTO_DELETE(g3);

    cout << "====================================================" << endl;
}

// ============================================================
//  3. 基本读写操作
// ============================================================
static void demo_read_write() {
    cout << "\n========== 3. 读写操作 ==========" << endl;

    auto* rw = new syscallRW(baseRW::PidMode::Private, getPID("com.example.target"));
    if (rw->getProcessPid() < 0) {
        cout << "[-] PID 无效，跳过读写演示。" << endl;
        AUTO_DELETE(rw);
        return;
    }

    /*
     * 你需要把 demoAddr 替换为目标进程中一个有效的地址。
     * 下面列出所有 API 供参考。
     */
    uintptr_t demoAddr = 0x77B6866000;  // 替换为真实地址！
    (void)demoAddr;

    // ---- 读取 ----
    // int val       = rw->getDword(addr);        读取 4 字节整数
    // float fval    = rw->getFloat(addr);        读取 4 字节浮点数
    // bool bval     = rw->getBool(addr);         读取 1 字节布尔值
    // uintptr_t p64 = rw->getPtr64(addr);        读取 8 字节指针（64位）
    // uintptr_t p32 = rw->getPtr32(addr);        读取 4 字节指针（32位）
    // char* str     = rw->getUTF8(addr);         读取 UTF-16 字符串（转 UTF-8，最多 14 字符）

    // ---- 写入 ----
    // rw->writeFloat(addr, 3.14f);               写入浮点数

    // ---- 原始读写（自定义结构体/数组） ----
    // int buf[4];
    // rw->readv(addr, buf, sizeof(buf));
    // rw->writev(addr, buf, sizeof(buf));

    cout << "[*] 使用前请将上方 demoAddr 替换为目标进程中的有效地址" << endl;

    AUTO_DELETE(rw);
    cout << "=================================================" << endl;
}

// ============================================================
//  4. 指针链（jumpPoint）
// ============================================================
static void demo_pointer_chain() {
    cout << "\n========== 4. 指针链 ==========" << endl;

    auto* rw = new syscallRW(baseRW::PidMode::Private, getPID("com.example.target"));
    if (rw->getProcessPid() < 0) {
        cout << "[-] PID 无效，跳过指针链演示。" << endl;
        AUTO_DELETE(rw);
        return;
    }

    /*
     * jumpPoint 模拟多级指针解引用：
     *
     *   [module_base + offset0]  ->  ptr1
     *   [ptr1        + offset1]  ->  ptr2
     *   [ptr2        + offset2]  ->  ptr3
     *   [ptr3        + offset3]  =   最终值
     *
     * 用法:  rw->jumpPoint(baseAddr, offset0, offset1, offset2, ...) + 最终偏移;
     *        相当于: *(*(*(base + 0x1A4) + 0x2B8) + 0x3C0) + 0x10
     *
     * 所有偏移必须是 int 类型（编译期 static_assert 检查）
     * 每步解引用时地址会被掩码到 40 位 (0xFFFFFFFFFF)
     */

    // 示例（注释掉了 — 替换为真实地址）：
    // uintptr_t base = rw->get_module_base("libil2cpp.so");
    // if (base == 0) {
    //     cout << "[-] 模块未找到" << endl;
    // } else {
    //     uintptr_t value = rw->jumpPoint(base, 0x1A4, 0x2B8, 0x3C0) + 0x10;
    //     cout << "[jumpPoint] 结果地址: 0x" << hex << value << dec << endl;
    // }

    cout << "[*] 替换为真实地址来测试指针链" << endl;

    AUTO_DELETE(rw);
    cout << "============================================" << endl;
}

// ============================================================
//  5. 模块基址
// ============================================================
static void demo_module_base() {
    cout << "\n========== 5. 模块基址 ==========" << endl;

    auto* rw = new syscallRW(baseRW::PidMode::Private, getPID("com.example.target"));
    if (rw->getProcessPid() < 0) {
        cout << "[-] PID 无效，跳过模块基址演示。" << endl;
        AUTO_DELETE(rw);
        return;
    }

    /*
     * get_module_base() 通过解析目标进程的 /proc/<PID>/maps
     * 获取共享库的加载基址。
     *
     * 用法:  rw->get_module_base("libil2cpp.so");
     *        rw->get_module_base("libunity.so");
     *        rw->get_module_base("libc.so");
     */

    // 示例（注释掉了 — 替换为真实模块名）：
    // uintptr_t base = rw->get_module_base("libc.so");
    // if (base != 0)
    //     cout << "[Module] libc.so 基址: 0x" << hex << base << dec << endl;
    // else
    //     cout << "[-] 模块未找到" << endl;

    cout << "[*] 替换为真实模块名来测试" << endl;

    AUTO_DELETE(rw);
    cout << "=============================================" << endl;
}

// ============================================================
//  6. copyRW — 同进程内存访问（无需 root，无需驱动）
// ============================================================
static void demo_copyRW() {
    cout << "\n========== 6. copyRW（同进程读写） ==========" << endl;

    /*
     * copyRW 通过 memcpy 读写当前进程内存。
     * 不需要 root 或内核驱动，但只能访问自身进程的内存。
     * 适用于自修改代码、hook 或测试。
     */

    // 分配一个测试变量
    int testValue = 42;
    uintptr_t addr = reinterpret_cast<uintptr_t>(&testValue);

    auto* rw = new copyRW();

    // 读回来
    int readback = rw->getDword(addr);
    cout << "[copyRW]  原始值: " << testValue << ", 读回值: " << readback << endl;

    // 写入新值
    rw->writeFloat(addr, 3.14f);
    cout << "[copyRW]  写入 writeFloat 后: " << testValue << "  (预期 ~3.14)" << endl;

    // 同进程内指针链示例
    int a = 10, b = 20, c = 30;
    int* pointers[] = { &a, &b, &c };
    uintptr_t chainAddr = reinterpret_cast<uintptr_t>(pointers);
    // 解引用: pointers → pointers[0] → a
    uintptr_t result = rw->jumpPoint(chainAddr, 0x0);
    cout << "[copyRW]  指针链结果: " << *(int*)result << "  (预期 10)" << endl;

    AUTO_DELETE(rw);
    cout << "===============================================" << endl;
}

// ============================================================
//  7. TGodRW — 自研内核模块（inet_ioctl 通道，需要 root）
// ============================================================
static void demo_tgodrw() {
    cout << "\n========== 7. TGodRW（自研驱动读写） ==========" << endl;

    /*
     * TGodRW 通过 inet socket 的 ioctl 与内核模块 DiDevice.kpm 通讯。
     * 构造时自动 CONNECT 握手，isConnected() 可查询握手结果。
     * 需求：root + 设备上已加载 DiDevice.kpm（push 后用 kpatch 加载）。
     *
     * 读模式作用域与 pid 共用同一个 mode：
     *   Global  —— setMod 改静态全局，对所有 Global 实例生效
     *   Private —— 读模式存实例内，多线程各用各的互不干扰
     */
    auto* rw = new TGodRW(baseRW::PidMode::Private, getPID("com.example.target"));
    if (!rw->isConnected() || rw->getProcessPid() < 0) {
        cout << "[-] 驱动未连接或 PID 无效，跳过演示。" << endl;
        AUTO_DELETE(rw);
        return;
    }

    // 模块基址（当前走 /proc/<pid>/maps，驱动 0x11405 尚未实现）
    uintptr_t base = rw->get_module_base("libc.so");
    if (base == 0) {
        cout << "[-] 模块未找到" << endl;
        AUTO_DELETE(rw);
        return;
    }
    cout << "[TGodRW]  libc.so 基址: 0x" << hex << base << dec << endl;

    // 读（默认有缓存）
    int value = 0;
    if (rw->readv(base, &value, sizeof(value)))
        cout << "[TGodRW]  有缓存读取成功: " << value << endl;
    else
        cout << "[TGodRW]  有缓存读取失败" << endl;

    // 无缓存读取：切模式读完切回（Private 只影响本实例，Global 影响所有 Global 实例）
    rw->setMod(TGodRW::ReadMode::NO_CACHE);
    int value2 = 0;
    if (rw->readv(base, &value2, sizeof(value2)))
        cout << "[TGodRW]  无缓存读取成功: " << value2 << endl;
    else
        cout << "[TGodRW]  无缓存读取失败" << endl;
    rw->setMod(TGodRW::ReadMode::NORMAL);

    // 多线程场景建议每线程一个 Private 实例：
    // auto* rw2 = new TGodRW(baseRW::PidMode::Private, pid);
    // rw2->setMod(TGodRW::ReadMode::NO_CACHE);  // 只影响 rw2

    // 写入并读回验证
    int magic = 0x114514;
    if (rw->writev(base, &magic, sizeof(magic))) {
        int readback = 0;
        rw->readv(base, &readback, sizeof(readback));
        cout << "[TGodRW]  写入并读回: 0x" << hex << readback << dec
             << (readback == magic ? "  (验证一致)" : "  (验证不一致!)") << endl;
    } else {
        cout << "[TGodRW]  写入失败" << endl;
    }

    AUTO_DELETE(rw);
    cout << "===============================================" << endl;
}

// ============================================================
//  8. TwTRW — TwT 内核驱动（anon_inode ioctl 通道，需要 root）
// ============================================================
static void demo_twtrw() {
    cout << "\n========== 8. TwTRW（TwT 驱动读写） ==========" << endl;

    /*
     * TwTRW 对接 TwT 内核驱动：驱动 hook 了 reboot 系统调用，
     * 构造时用魔数分支下发 anon_inode fd，之后通过 ioctl 通讯。
     * isConnected() 可查询对接结果；需要 root + 设备上已加载 TwT 驱动。
     *
     * 构造函数第 4/5 个参数可独立启用陀螺仪和触摸（-1 不启用，默认）：
     *   new TwTRW(mode, pid, baseRW::PidMode::Global, 0, 1);  // 陀螺仪 tracepoint + 触摸模式 1
     * 也可以构造后单独调 gyro_init() / touch_init()。
     *
     * 读模式作用域独立于 pid（第三个构造参数，默认 Global）：
     *   MOD1（默认）走 READ_MEM；MOD2 走 READ_MEM_V2 —— 驱动的两条读取通道
     *   Global（默认）—— setMod 改静态全局；Private —— 读模式存实例内互不干扰
     *
     * 驱动附带功能（详见 TwTRW.hpp）：
     *   get_pid_by_name()        — 驱动侧按进程名取 PID
     *   get_module_bss()         — 驱动侧取模块 .bss 基址
     *   touch_init/down/up       — 触摸注入
     *   gyro_init/modify/disable — 陀螺仪
     *   bp_inst/bp_get_hits/...  — 硬件断点/观察点
     */
    auto* rw = new TwTRW(baseRW::PidMode::Private, getPID("com.example.target"));
    if (!rw->isConnected() || rw->getProcessPid() < 0) {
        cout << "[-] 驱动未连接或 PID 无效，跳过演示。" << endl;
        AUTO_DELETE(rw);
        return;
    }

    // 模块基址（驱动侧 MODULE_BASE）
    uintptr_t base = rw->get_module_base("libc.so");
    if (base == 0) {
        cout << "[-] 模块未找到" << endl;
        AUTO_DELETE(rw);
        return;
    }
    cout << "[TwTRW]   libc.so 基址: 0x" << hex << base << dec << endl;

    // 读取（默认 MOD1 通道）
    int value = 0;
    if (rw->readv(base, &value, sizeof(value)))
        cout << "[TwTRW]   MOD1 读取成功: " << value << endl;
    else
        cout << "[TwTRW]   MOD1 读取失败" << endl;

    // 切到 MOD2 通道（READ_MEM_V2）读取：Global 模式下切全局，读完切回
    rw->setMod(TwTRW::ReadMode::MOD2);
    int value2 = 0;
    if (rw->readv(base, &value2, sizeof(value2)))
        cout << "[TwTRW]   MOD2 读取成功: " << value2 << endl;
    else
        cout << "[TwTRW]   MOD2 读取失败" << endl;
    rw->setMod(TwTRW::ReadMode::MOD1);

    // 多线程场景建议每线程一个 Private 读模式的实例：
    // auto* rw2 = new TwTRW(baseRW::PidMode::Private, pid, baseRW::PidMode::Private);
    // rw2->setMod(TwTRW::ReadMode::MOD2);  // 只影响 rw2

    // 写入并读回验证
    int magic = 0x114514;
    if (rw->writev(base, &magic, sizeof(magic))) {
        int readback = 0;
        rw->readv(base, &readback, sizeof(readback));
        cout << "[TwTRW]   写入并读回: 0x" << hex << readback << dec
             << (readback == magic ? "  (验证一致)" : "  (验证不一致!)") << endl;
    } else {
        cout << "[TwTRW]   写入失败" << endl;
    }

    // 断点示例（替换为真实地址）：
    // rw->bp_init_driver();
    // uint64_t handle = rw->bp_inst(pid, addr, TWT_BP_LEN_4, TWT_BP_RW);
    // auto hits = rw->bp_get_hits(handle);
    // rw->bp_uninst(handle);

    AUTO_DELETE(rw);
    cout << "===============================================" << endl;
}

// ============================================================
//  主函数
// ============================================================
int main() {
    cout << "============================================" << endl;
    cout << "  diRW — 多后端内存读写示例" << endl;
    cout << "============================================" << endl;
    cout << "使用前请在源码中设置目标 PID 和地址        " << endl;
    cout << "============================================" << endl;

    demo_backend_selection();
    demo_pid_mode();
    demo_read_write();
    demo_pointer_chain();
    demo_module_base();
    demo_copyRW();
    demo_tgodrw();
    demo_twtrw();

    cout << "\n===== 演示结束 =====" << endl;
    return 0;
}
