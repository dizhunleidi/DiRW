# 更新日志

本项目遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 格式，
版本号遵循语义化版本。

## [2.62] - 2026-09-13

### 变更
-TGodRW的获取基质功能改为内核
-优化了例子的cmakeliat，方便编译与vscode配置

## [2.61] - 2026-09-10

### 变更

- 去掉了TGodRW单独的读取模式的设置范围。直接使用统一的pidmode来控制读写方式的修改方式和pid的修改方式

## [2.6] - 2026-08-30

### 新增

- **TwTRW 后端**：对接 TwT 内核驱动。fd 由驱动的 reboot 系统调用魔数分支下发
  （anon_inode，`MY_CALL` 内联 syscall + `/proc/self/fd` 扫描兜底），通过 `ioctl`
  通信，支持 `READ_MEM`/`READ_MEM_V2`/`WRITE_MEM`/`MODULE_BASE`/`MODULE_BSS`/`GET_PID`
- **读模式**：作用域处理与 `TGodRW` 一致——`MOD1` 走 `READ_MEM`、`MOD2` 走
  `READ_MEM_V2`（驱动的两条读取通道），读模式作用域复用 PID 的 Global/Private
  语义（构造第三个参数），可全局切换（`setGlobalMod`）或按实例独立，多线程互不干扰
- **驱动附带功能**：触摸注入（`touch_init`/`touch_down`/`touch_up`）、陀螺仪
  （`gyro_init`/`gyro_modify`/`gyro_disable`）、硬件断点/观察点全生命周期管理
  （`bp_inst`/`bp_modify`/`bp_get_hits` 等，含 `TwtRegBatch` 批量寄存器修改辅助）
- **独立初始化**：构造函数第 4/5 个参数 `gyro_mode`/`touch_mode` 可在构造时分别
  独立启用陀螺仪与触摸（`-1` 不启用，默认），也可构造后单独调用各自 init；
  任一初始化失败不影响另一个与后续读写
- **示例**：`example.cpp` 新增 TwTRW 演示（对接检查、模块基址、读写验证）

### 变更

- 相比驱动的参考实现 `c_driver`：构造不再交互式询问陀螺仪/触摸初始化，也不再
  `exit()` 进程，统一走 `connected` 标志 + `isConnected()` 查询；触摸/陀螺仪改为
  显式调用对应 init 方法

## [2.5] - 2026-08-29

### 新增

- **TGodRW 后端**：对接自研内核模块 DiDevice.kpm（hook `inet_ioctl`，通过任意 inet
  socket 的 `ioctl` 通信），支持有缓存/无缓存读取与写入内存
- **读模式作用域**：`TGodRW` 的有缓存/无缓存读模式复用 PID 的 Global/Private 语义，
  可全局切换（`setGlobalMod`）或按实例独立（构造第三个参数），多线程互不干扰
- **CMake 构建**：新增 `CMakeLists.txt`，与 `Android.mk` 等价，兼容命令行与
  Android Studio `externalNativeBuild`

### 变更

- **连接状态统一管理**：`connected` 标志上收到 `baseRW` 基类，`isConnected()` 查询；
  驱动对接失败不再退出进程（移除 `exit(1)` 式错误处理），由调用方决定后续逻辑
- **线程安全**：全局 PID（`baseRW::globalPid`）与 TGod 读模式（`globalMod`）改为
  `std::atomic` + relaxed 序，多线程切换立即可见
- **修复**：`Qx11RW`/`RtRW` 的 `fd` 成员未初始化，构造失败路径下析构可能 close 随机句柄
- **文档**：修正 `syscallRW` 的 root 要求描述（跨 UID 进程需要 root）；README 按
  仓库新布局重写

### 移除

- **leidiRW 后端**：由 TGodRW 取代
