# AI Coding 开发记录 — contest2026_471_sibakenailongshiyanshi

> **说明**：本目录为队伍在开发过程中的 AI 辅助记录。本队全程使用豆包（Doubao）桌面端 AI 助手完成 BSP 移植、驱动开发与排障，豆包不在官方采集器（Claude Code / OpenCode / Codex / AIoT-IDE）支持列表内，因此本目录下的记录为**人工整理的开发过程摘要（Markdown），而非采集器导出的 JSONL 会话原文**。如需采集器格式的官方会话日志，请安装 `contest-log-collector` 并使用官方支持工具进行开发。

- 队伍编号：471
- 队伍名：sibakenailongshiyanshi（赛博乃龙实验室）
- 平台：慧勤智远 GD32H759IMT6（V1.3 小系统板，Cortex-M7 @ 600MHz）
- 方向：新硬件适配（openvela / NuttX 移植）
- 开发周期：2026-08 至 2026-09

---

## 一、开发时间线

| 日期 | 阶段 | 关键产出 |
|---|---|---|
| 2026-08 下旬 | 环境与基线 | openvela 工程同步、构建链打通（nuttx.bin/hex 产出）、NSH 串口基线固件 |
| 2026-08 下旬 | LED 适配 | userleds 驱动，红/绿灯交替（曾遇到绿灯不亮：GPIO AHB4EN/GPIOH OCTL 配置问题，已解决） |
| 2026-08 底 | SDRAM | EXMC 外部 SDRAM W9825G6KH 初始化，0xC0000000 映射，读写回读验证通过 |
| 2026-09 初 | GT911 触摸 | 软 I2C 打通、GT911/GT1158 坐标解析（GSTID=0x81 判定、逐字节比对例程与 Linux goodix.c） |
| 2026-09 初 | TLI LCD | 新增 TLI 并行 RGB LCD 驱动，800x480 RGB565，运行时按需开启（`lcd` 命令）；显示质量仍在调试（水平伴线问题） |
| 2026-09-04 | 作品入仓 | PR #1 merged（board/app/README），CLA 签署（commit 邮箱对齐 2191360860@qq.com） |

## 二、各模块开发与排障要点

### 1. 系统移植与构建

- 基于 openvela（NuttX 变体）gd32h7 架构，新增 `gd32h759imt6` 板级支持，`CONFIG_GD32H759IMT6_*` 板级使能。
- 构建命令：`./build.sh vendor/gigadevice/boards/gd32h7/gd32h759imt6/configs/nsh/defconfig -j8`，产物 `nuttx.bin/.hex`。
- 时钟树：SYSCLK=600MHz（PLL0），HCLK=300MHz，TICK 正常；TLI 像素时钟由 PLL2 提供（33MHz，PSC=25/N=396/P=3/R=3 → VCO=396MHz → PLL2R=132MHz → /4=33MHz）。

### 2. EXMC SDRAM（W9825G6KH）

- 配置要点：CAW=9bit 列、RAW=13bit 行、16bit 数据、4 bank、CAS=3、SDCLK=CK_EXMC/2、突发读使能、流水线 2。
- 时序：LMRD=2/XSRD=11/RASD=10/ARFD=10/WRD=2/RPD=3/RCD=3（写入硬件字段值为延迟-1，与 GD 库一致）。
- 刷新间隔：8192 行 / 64ms → 1151 SDCLK 周期（写入 SDARI bit1 起，即 0x8FE）。
- 初始化序列：时钟使能 → 预充电 → 自动刷新 ×8（编码 7）→ 装载模式寄存器（burst=1, sequential, CAS=3 → 0x30）。
- 排障：热复位后卡死（SDRAM 残留状态）→ 初始化前先关 SDCLK 等待 2ms 规避；384000 像素回读 0 errors。

### 3. userleds（红/绿灯）

- 绿灯不亮根因：GPIO 端口时钟未使能（AHB4EN 中 PJEN 未置位）、GPIOH OCTL 未正确配置；修复后红绿灯交替正常。
- 依用户要求静默化（去掉刷屏日志）。

### 4. GT911 / GT1158 电容触摸

- 软 I2C 时序打通（板载无硬件 I2C 或复用问题），地址探测 0x28/0x29/0x38/0x39/0x70/0x71。
- 产品 ID 读取：0x1158（"115"）；初始化需要完整复位时序（gpio → rst → settle → pid → probe，约 6370 ticks）。
- 坐标解析关键：GSTID=0x81 表示有触摸，TP1 6 字节（状态/触摸ID/高位低位组合）→ 按 `X=b1<<8|b2, Y=b3<<8|b4` 解析；多次与例程 tli.c 及 Linux drivers/input/touchscreen/goodix.c 逐字节比对确认。
- 曾出现：初始化慢（几十秒）、无反应（坐标全 0）、越界（超过 800 的坐标）——分别由复位时序缺失、GSTID 判定错误、像素映射公式错误导致，均已修正。

### 5. TLI 并行 RGB LCD（800x480，进行中）

- 新增 `gd32h7xx_tli.c/.h` 驱动：SDRAM framebuffer 0xC0000000、800x480 RGB565。
- 时序（与屏幕规格书 WKS43WV067 及原厂 F429 参考工程一致）：HSW=48/HBP=88/HFP=40/VSW=3/VBP=32/VFP=13；HS/VS/DE 低有效、PCLK 不翻转。
- 层配置：窗口 136..935 / 35..514、LXSA=255、ACF=PASA、FLL=1671/STDOFF=1664、FTLN=480。
- 使能顺序：清 dither → layer enable → reload → TLI enable → 面板复位（10/50/200ms，DWT 自旋防卡死）→ 背光。
- **未解决问题**：显示有水平伴线（主线两侧断续虚线）。已系统排查并排除：framebuffer/SDRAM 数据路径（回读全对）、中断干扰（关中断测试）、PCLK 频率（33→16.5MHz 仍存在）、STDOFF（1600 vs 1664 对比）、TLI 寄存器与例程/原厂工程全部一致、DMA/MPU/D-Cache 均未使能。当前在进行写入竞态（画图时关 TLI）与 GD 库直移对照实验。

### 6. 作品提交

- 参赛仓：open-vela/contest2026_471_sibakenailongshiyanshi（分支 dev-ai-contest-2026），fork：QQQuer/…（分支 port/gd32h759-v1）。
- 流程：fork → 开发提交 → PR → cla/signature 检查 → merged（merge commit e329f995679f7050ed7e709e3b7b56f1c44376c8）。
- **CLI/CLA 教训**：PR 的 cla/signature 按 commit 作者邮箱匹配 CLA 签署邮箱；commit 必须使用主邮箱 2191360860@qq.com（noreply 邮箱会被拒），签署页 https://openvela.com/#/community/cla，PR 评论 `/check-cla` 复检。
- 作品内容：`board/contest_board/`（bringup/GT911/userleds + nuttx gd32h7xx TLI/SDRAM/GPIO 改动）、`app/{gt911_demo,lcd_demo,leds_demo}`、README。

## 三、AI 使用说明

- 工具：豆包桌面端（Windows），通过其本地环境执行 SSH/SCP 到 Ubuntu VM（openvela 工程）完成构建、烧录与 GitHub 操作。
- 典型用法：方案设计（BSP 移植步骤）、驱动编写（寄存器级）、串口日志归因（刷屏/卡死/越界）、例程与第三方源码逐字节对照（GT911、TLI、SDRAM 时序）、实验设计（单变量对照烧录验证）。
- 效率点：位域推算（SDTCFG/SDARI/PLL 寄存器）、时序窗口分析（软 I2C、面板复位忙等）、快速迭代烧录（VM 编译 → 本地 .bin/.hex → 用户烧录反馈）。

## 四、合规说明

- 本目录记录为人工整理的开发过程摘要，**非官方采集器导出的会话原文（JSONL）**。
- 官方 `validate-log.py` 校验的对象是采集器格式 JSONL；本目录不包含该类文件，故不存在篡改/伪造问题。
- 如需官方"有效工时"统计，请使用 Claude Code / OpenCode / Codex / AIoT-IDE 在 openvela 工作区内进行后续开发（安装 `contest-log-collector` 后自动归集）。
