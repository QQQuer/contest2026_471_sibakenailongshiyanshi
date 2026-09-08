# GD32H759IMT6 openvela（NuttX）硬件适配 — 赛题 471

## 一、作品简介

在慧勤智远 GD32H759IMT6（V1.3 小系统板，Cortex-M7 @ 600MHz）上完成 **openvela（NuttX 变体）的硬件适配与工业触控可视化系统**：跑通外部 SDRAM（W9825G6KH）、板上 LED、按键、GT911/GT1158 电容触摸、TLI 并行 RGB LCD（800x480 WKS43WV067），并基于 framebuffer 自绘一套 **白底 GUI 可视化系统**（主界面 + 触摸跟随 / 测量控制 / 系统信息子页面），当前正在开发 **ADC 采集 + FFT 频谱分析** 功能（板载 DAC 自测回环，免外接信号源）。

## 二、选题方向

**新硬件适配**。将 openvela（NuttX）完整移植到国产 GD32H759IMT6 微控制器平台，覆盖 BSP、外设驱动、NSH 基础运行环境、板级使能（bringup）与工业触控上层应用。

## 三、目录结构

- `board/contest_board/` — 板级适配代码
  - `src/` — board 层源码：`gd32h7xx_bringup.c`（含按需 `board_lcd_enable`）、`gd32h7xx_gt911.c`（软 I2C 触摸驱动，v49 稳定解码版）、`gd32h7xx_userleds.c`（LED，已静默化）
  - `nuttx/arch/arm/src/gd32h7xx/` — 内核驱动改动：`gd32h7xx_tli.c/.h`（新增 TLI 显示驱动）、`gd32h7xx_sdram.c`（EXMC SDRAM，SDCTL=0x59d9 消除伴线伪影）、`gd32h7xx_gpio.h`（GD32H759 端口编码修复）
- `app/gt911_demo/` — GT911 触摸检测示例（`gt911` 命令）
- `app/lcd_demo/` — 运行时开启 LCD 示例（`lcd` 命令，boot 不初始化、按需使能）
- `app/leds_demo/` — LED 示例（`leds` 命令，完全静默）
- `app/gui/` — GUI 可视化系统（`gui` 命令）：主界面 3 入口（触摸跟随 / 测量控制 / 系统信息），自绘白底 UI + 微软雅黑风格字库 + 触摸校准（9 点）
- `firmware/` — 已交付固件（bin），版本说明见下
- `logs/` — AI Coding 日志（持续更新）

## 四、运行方式

1. 按 README 顶部流程 `repo init` / `repo sync` 拉取 openvela 全量源码与本仓。
2. 将本仓代码应用到对应路径（均为增量改动）：
   - `board/contest_board/src/*` → `vendor/gigadevice/boards/gd32h7/gd32h759imt6/src/`
   - `board/contest_board/nuttx/arch/arm/src/gd32h7xx/*` → `nuttx/arch/arm/src/gd32h7xx/`
   - `app/gt911_demo/*` → `apps/examples/gt911/`；`app/lcd_demo/*` → `apps/examples/lcd/`；`app/leds_demo/leds_main.c` → `apps/examples/leds/`；`app/gui/*` → `apps/examples/gui/`
3. 启用相关配置：`CONFIG_GD32H759IMT6_GT911`、`CONFIG_EXAMPLES_GT911`、`CONFIG_EXAMPLES_LCD`、`CONFIG_EXAMPLES_GUI`、`CONFIG_GD32H7_TLI`。
4. 编译：`./build.sh vendor/gigadevice/boards/gd32h7/gd32h759imt6/configs/nsh/defconfig -j8`。
5. 串口 115200 进入 NSH：
   - `leds` — LED 闪烁（无串口输出）
   - `gt911` — 触摸检测
   - `lcd` — 按需开启 LCD 并画测试图案
   - `gui` — 启动可视化系统（触摸校准后进入主界面；测量控制页内 `d` 键切换板载 DAC 自测正弦开关）

## 五、固件版本说明（firmware/）

| 版本 | 说明 | 状态 |
|---|---|---|
| `nuttx_v49.bin` | 稳定基线：3 按钮 GUI + 触摸可用 | ✅ 用户确认 |
| `nuttx_v55.bin` | 首个 ADC 采集 + FFT 频谱版（板载 DAC 自测正弦回环） | ⚠️ 采到直流，无波形 |
| `nuttx_v56.bin` | v55 + 采样范围统计/寄存器回读诊断 | ⚠️ 定位用 |
| `nuttx_v57.bin` | v56 + EOC 等待超时保护（不卡死）+ 诊断前置 | ⚠️ 定位用 |
| `nuttx_v58.bin` | v57 + DAC→ADC 回环探针（DAC 输出 0/1.65V/3.3V 三步验证） | 🔧 调试中 |

中间版本 v50-v54（触摸驱动大端解码尝试、手写识别调整）均被实测否定后回退，不收录。

### ADC 频谱功能（开发中）

- 采集：ADC0（0x40012400）14bit，通道 18 = PA4，软件触发单次转换，256 点
- 自测信号源：DAC0（0x40007400）DAC_OUT0 = 同一 PA4，32 点 12bit 正弦表，`d` 键开关，免外接信号源；外部信号可直接接 PA4
- 显示：左半屏时域波形，右半屏 FFT 频谱 128 条，右上角主峰频率
- 寄存器直操实现（参照官方例程 18/20），不依赖标准库，便于逐位排查
- **排障中**：v58 探针将验证 DAC→ADC 回环链路是否打通

## 六、AI Coding 使用说明

全程使用 AI 助手（豆包）辅助：BSP 移植方案设计、TLI 驱动编写与寄存器级排查、软 I2C 时序打通、GT911 坐标解析（逐字节比对例程与 Linux goodix.c）、LCD 伴线伪影定位（SDRAM 时序）、GUI 字库构建与触摸校准、ADC/DAC 寄存器直操与 FFT 频谱实现。AI 在驱动调试（位域推算、时序窗口分析）与代码量产出上显著提升了效率。完整对话日志见 `logs/`。

## 当前状态

- ✅ 稳定：外部 SDRAM、userleds（红/绿灯）、buttons、GT911 触摸（v49 解码基线）、TLI LCD 显示（800x480 RGB565，无伴线伪影）、白底 GUI（3 子页面 + 触摸校准 + 微软雅黑字库）
- 🔧 调试中：ADC 采集 + FFT 频谱（DAC 自测回环未出波形，v58 探针定位中）
- 🔧 历史排障：热复位卡死（面板复位忙等 → 按需初始化）、LED 刷屏、触摸初始化慢、LCD 伴线（SDRAM 时序）均已定位或规避
