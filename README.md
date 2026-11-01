# ec-round2-260411615 —— 四自由度舵机机械臂固件

> 学号 **260411615** ｜ 姓名 **万江东** ｜ 仓库 `ec-round2-260411615`
> CVIR 机甲大师 2026 电控组二轮考核
> 主控 **STM32F103C8T6**（江协科技最小系统核心板）｜CubeMX(CMake) + HAL + arm-none-eabi-gcc
>
> ⚠️ 本文件是骨架，**每完成一项就把对应小节填实**。任务书 5.1 对这些内容有硬要求，
> 评审会照它 clone 重建，填不实等于判构建失败。

---

## 1 构建（Debug / Release 两预设）

```bash
# Debug（默认，含调试符号，-Og）
cmake --preset Debug
cmake --build --preset Debug

# Release（-O2）
cmake --preset Release
cmake --build --preset Release
```

烧录（ST-Link，SWD）：

```bash
ST-LINK 烧录见 .vscode/tasks.json 的 "Flash (Debug)" 任务（Ctrl+Shift+B 选）
# 或命令行：
STM32_Programmer_CLI -c port=SWD -w build/Debug/ec-round2-260411615.elf -v -rst
```

> **`Ctrl+Shift+B` 只是编译，不烧板。** 编译成功看 `build/Debug/ec-round2-260411615.elf` 的时间戳；
> 烧录成功必须看到 `Download verified successfully` 或 `File download complete`。
> 这两个极易混淆（10-31 踩过两次）。

## 2 版本

| 项 | 版本 | 备注 |
|---|---|---|
| MCU | STM32F103C8T6（Cortex-M3，64KB Flash / 20KB RAM，LQFP48） | |
| STM32CubeMX | 6.1x.x | **必须 ≥ 6.12**，项目管理器选 **CMake** 工具链 |
| CMake | 4.4.0+st.1 | ≥ 3.22 |
| Ninja | 1.13.2+st.1 | |
| arm-none-eabi-gcc | 14.3.1+st.2 | STM32CubeCLT 自带版本 |
| C 标准 | **C17**（`CMAKE_C_STANDARD 17`） | |
| 调试器 | ST-Link V2 + ST-LINK_gdbserver 7.14.0+st.2 | |
| 系统时钟 | 72 MHz（HSE 8MHz × PLL9），APB1=DIV2，USB 48MHz | |

## 3 接线表

> 完整说明与"为什么这么接"见工作台 `00-控制台/PINMAP.md`。

| 功能 | MCU 引脚 | 外设 | 接到哪 |
|---|---|---|---|
| 底座舵机 | PA6 | TIM3_CH1 | SG90 信号（橙） |
| 肩舵机 | PA7 | TIM3_CH2 | SG90 信号（橙） |
| 肘舵机 | PB0 | TIM3_CH3 | SG90 信号（橙） |
| 夹爪舵机 | PB1 | TIM3_CH4 | SG90 信号（橙） |
| 舵机电源 | — | — | **独立 5V/3A，只共地** |
| 调试串口 | PA9 / PA10 | USART1 | USB-TTL：模块 RXD→PA9、TXD→PA10、GND 共地 |
| MPU6050 | PB6 / PB7 | I2C1 | SCL / SDA，400kHz，模块自带 4.7k 上拉 |
| USB | PA11 / PA12 | USB FS Device | 板载 micro-USB 座（已接死） |
| 按键 | PB12 | GPIO 输入 + 上拉 | 一端 PB12，一端 GND |
| LED | PC13 | GPIO 输出 | 板载，**低电平点亮** |

**供电顺序**：先接共地线 → 舵机电源上电 → 主控上电；断电相反。
**禁止**从板载 3.3V/5V 给舵机供电。

## 4 协议实现说明

> 完整逐字节表见工作台 `03-协议/protocol-v1.0.md`，扩展帧见 `protocol-v1.1-ext.md`。

- 帧格式：`0xA5 | type | N | payload[N] | CRC8`，小端，角度 0.1° int16
- CRC8：poly `0x07`、init `0x00`、不反射、不异或；**覆盖帧头到载荷末字节**
- 支持类型：`0x10` 状态上报 / `0x20` 目标角度 / `0x21` 模式切换 / `0x22` 急停 / `0x23` 心跳 / `0x24` 版本
- 协议代码位置：（待填，如 `protocol/protocol.c`、`protocol/crc8.c`）
- 传输层：CDC 走 `bsp_usb_cdc.c`；**协议与传输层分离，换链路不改协议与业务**
- 扩展帧 `0x30`–`0x33`：（待定，见工作台 DECISIONS D-15）
- 单元自测：`protocol` 的编解码已在 PC 上用 gcc 单测，覆盖任务书 5 条示例帧 + 1 条扩展示例，
  **逐字节一致**（详见工作台 `05-档案/2026-11-01-00-工作台搭建.md`）

## 5 演示视频

（待填链接）

## 6 AI 使用记录

见 **[AI.MD](AI.MD)**。评审会用 `git log --grep="^AI:"` 与 AI.MD 逐条对账。

## 7 目录结构

```
Core/            CubeMX 生成（只改 USER CODE 区）
Drivers/         HAL + CMSIS（CubeMX 生成，不改）
cmake/           工具链文件（CubeMX 生成，**不得手改**）
application/     标定、插补、状态机、故障处理   （待建）
bsp/             PWM / I2C / USART / GPIO / USB 传输层（待建）
components/      MPU6050 驱动、PID 等           （待建）
protocol/        帧编解码 + crc8，**全工程只有一份**（待建）
```

> 新增 `.c` 必须同时登记到根 `CMakeLists.txt` 的 `target_sources()` 和
> `target_include_directories()`，否则别人 clone 下来构建失败。

## 8 仓库放什么、不放什么

**原则：这个仓库里只有"能讲清、能交付"的东西。** 其余一律放仓库外的草稿箱。

| | 放仓库 | 放草稿箱 `E:\STM32\_scratch\` |
|---|---|---|
| 源码 | `application/` `bsp/` `components/` `protocol/` 里的正式代码 | 临时验证用的实验代码、别人代码的片段 |
| 工程 | `.ioc`、`CMakeLists.txt`、`CMakePresets.json`、`cmake/`、`Drivers/`、`Core/` | 单独的 HAL 小实验工程（`hal-lab\`） |
| 测试 | —（本工程无仓库内单测） | 协议编解码的 PC 单测、CRC 对比脚本 |
| 文档 | `README.md`、`AI.MD`、`.vscode/`、`.clangd`、`.gitignore` | 半成品笔记、截图、抓包记录 |
| 产物 | —（一律不提交） | 日志、CSV、串口抓包、导出的二进制 |

`.gitignore` 里已加防御规则（`_scratch/` `lab*/` `*_test.c` `*_backup*` 等），
但**真正的隔离靠目录分离，不靠 `.gitignore`** —— 它挡不住 `git add -f` 和 IDE 的"添加全部"。

### 从草稿箱转正的固定动作

1. 在 `_scratch` 里跑通、验证过；
2. 复制到仓库正确位置，**按本仓库规范重写**（`uint8_t` 不用 `u8`、头文件守卫唯一、C17）；
3. 登记进 `CMakeLists.txt` 的两处（`target_sources()` + `target_include_directories()`）；
4. commit，message 写清功能，末尾带 `AI:` / `Verified:`。

### 提交前 60 秒自查

```bash
git status --short          # 有没有冒出奇怪的文件
git ls-files | grep -Ei "_scratch|lab|test_|backup|copy of"   # 应为空
ls *.uvprojx *.ewp 2>/dev/null                                # 应为空（Keil/IAR 禁止）
```

