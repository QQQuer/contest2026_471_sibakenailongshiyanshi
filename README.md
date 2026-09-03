# GD32H759IMT6 openvela（NuttX）硬件适配 — 赛题 471

## 一、作品简介

在慧勤智远 GD32H759IMT6（V1.3 小系统板，Cortex-M7 @ 600MHz）上完成 **openvela（NuttX 变体）的硬件适配**：跑通外部 SDRAM（W9825G6KH）、板上 LED、按键、GT911/GT1158 电容触摸，并**新增 TLI 并行 RGB LCD 控制器驱动**与运行时按需开启 LCD 的命令。当前提供**稳定的无 LCD 基线固件**（触摸功能完整可用），LCD 显示正在进一步调试中。

## 二、选题方向

**新硬件适配**。将 openvela（NuttX）完整移植到国产 GD32H759IMT6 微控制器平台，覆盖 BSP、外设驱动、NSH 基础运行环境与板级使能（bringup）。

## 三、目录结构

- `board/contest_board/` — 板级适配代码
  - `src/` — board 层源码：`gd32h7xx_bringup.c`（含按需 `board_lcd_enable`）、`gd32h7xx_gt911.c`（软 I2C 触摸驱动）、`gd32h7xx_userleds.c`（LED，已静默化）
  - `nuttx/arch/arm/src/gd32h7xx/` — 内核驱动改动：`gd32h7xx_tli.c/.h`（新增 TLI 显示驱动）、`gd32h7xx_sdram.c`（EXMC SDRAM）、`gd32h7xx_gpio.h`（GD32H759 端口编码修复）
- `app/gt911_demo/` — GT911 触摸检测示例（`gt911` 命令，按任意键退出）
- `app/lcd_demo/` — 运行时开启 LCD 示例（`lcd` 命令，boot 不初始化、按需使能）
- `app/leds_demo/` — LED 示例（`leds` 命令，完全静默）
- `logs/` — AI Coding 日志（持续更新）

## 四、运行方式

1. 按 README 顶部流程 `repo init` / `repo sync` 拉取 openvela 全量源码与本仓。
2. 将本仓代码应用到对应路径（均为增量改动，不修改公共仓编译配置之外的逻辑）：
   - `board/contest_board/src/*` → `vendor/gigadevice/boards/gd32h7/gd32h759imt6/src/`
   - `board/contest_board/nuttx/arch/arm/src/gd32h7xx/*` → `nuttx/arch/arm/src/gd32h7xx/`
   - `app/gt911_demo/*` → `apps/examples/gt911/`；`app/lcd_demo/*` → `apps/examples/lcd/`；`app/leds_demo/leds_main.c` → `apps/examples/leds/`
3. 启用相关配置：`CONFIG_GD32H759IMT6_GT911`、`CONFIG_EXAMPLES_GT911`、`CONFIG_EXAMPLES_LCD`、`CONFIG_GD32H7_TLI`（可选）。
4. 编译：`./build.sh vendor/gigadevice/boards/gd32h7/gd32h759imt6/configs/nsh/defconfig -j8`。
5. 串口 115200 进入 NSH：`leds` 闪烁、`gt911` 触摸坐标、`lcd` 按需开启屏幕。

## 五、AI Coding 使用说明

全程使用 AI 助手（豆包）辅助：BSP 移植方案设计、TLI 驱动编写与寄存器级排查、软 I2C 时序打通、GT911 坐标解析（逐字节比对例程与 Linux goodix.c）、串口日志归因（刷屏/卡死定位）等。AI 在驱动调试（位域推算、时序窗口分析）与代码量产出上显著提升了效率。完整对话日志见 `logs/`。

## 当前状态

- ✅ 稳定：外部 SDRAM、userleds（红/绿灯）、buttons、GT911 触摸（坐标解析 + 固定映射）、NSH 基础
- 🚧 调试中：TLI LCD 显示（水平重复/拉伸问题）、运行时按需开启 LCD
- 🔧 历史排障：热复位卡死（面板复位忙等）、LED 刷屏、触摸初始化慢等均已定位或规避
