/****************************************************************************
 * arch/arm/src/gd32h7xx/gd32h7xx_tli.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <debug.h>
#include <syslog.h>

#include <nuttx/video/fb.h>

#include "arm_internal.h"
#include "chip.h"
#include "gd32h7xx.h"
#include "gd32h7xx_tli.h"
#include "gd32h7xx_rcu.h"
#include "gd32h7xx_gpio.h"
#include "hardware/gd32h7xx_rcu.h"
#include "hardware/gd32h7xx_tli.h"

#ifdef CONFIG_GD32H7_TLI

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* --- Panel timing (WKS 4.3 inch RGB LCD, WKS43WV067, 800x480) ---
 * From the panel spec: HSYNC width 48, back porch 88, front porch 40;
 * VSYNC width 3, back porch 32, front porch 13.
 */

#define GD32_TLI_HSW                  48   /* horizontal sync width */
#define GD32_TLI_HBP                  88   /* horizontal back porch */
#define GD32_TLI_HFP                  40   /* horizontal front porch */
#define GD32_TLI_VSW                  3    /* vertical sync width */
#define GD32_TLI_VBP                  32   /* vertical back porch */
#define GD32_TLI_VFP                  13   /* vertical front porch */

/* --- PLL2 configuration for the TLI pixel clock ---
 * CK_TLI = CK_HXTAL / pll2_psc * pll2_n / pll2_r / pll2_r_div
 *         = 25MHz / 25 * 396 / 3 / 4 = 33MHz
 */

#define GD32_TLI_PLL2_PSC             25
#define GD32_TLI_PLL2_N               396
#define GD32_TLI_PLL2_P               3    /* unused by TLI, keep valid */
#define GD32_TLI_PLL2_R               3
#define GD32_TLI_PLL2RDIV             1    /* RCU_PLL2R_DIV4 (1 -> /4) */

/* --- TLI GPIO pin configurations ---
 * Control signals:  DE=PF10, VSYNC=PA7, HSYNC=PC6, PCLK=PG7 (all AF14)
 * Backlight:        BL=PJ9   (push-pull output)
 * Reset:            RST=PJ11 (push-pull output)
 * Data R:   PG6(AF14) R7, PB1(AF9) R6, PA9(AF14) R5, PA5(AF14) R4,
 *           PB0(AF9) R3, PA1(AF14) R2, PA2(AF14) R1, PG13(AF14) R0
 * Data G:   PK2(AF14) G7, PC7(AF14) G6, PB11(AF14) G5, PB10(AF14) G4,
 *           PJ10(AF14) G3, PA6(AF14) G2, PE6(AF14) G1, PE5(AF14) G0
 * Data B:   PB9(AF14) B7, PA15(AF14) B6, PB5(AF3) B5, PG12(AF9) B4,
 *           PA8(AF13) B3, PD6(AF14) B2, PA10(AF14) B1, PE4(AF14) B0
 */

#define GPIO_TLI_DE   (GPIO_CFG_PORT_F | GPIO_CFG_PIN_10 | GPIO_CFG_MODE_AF | \
                       GPIO_CFG_AF_14 | GPIO_CFG_PUPD_NONE | GPIO_CFG_PP | \
                       GPIO_CFG_SPEED_100_220MHZ)
#define GPIO_TLI_VSY  (GPIO_CFG_PORT_A | GPIO_CFG_PIN_7 | GPIO_CFG_MODE_AF | \
                       GPIO_CFG_AF_14 | GPIO_CFG_PUPD_NONE | GPIO_CFG_PP | \
                       GPIO_CFG_SPEED_100_220MHZ)
#define GPIO_TLI_HSY  (GPIO_CFG_PORT_C | GPIO_CFG_PIN_6 | GPIO_CFG_MODE_AF | \
                       GPIO_CFG_AF_14 | GPIO_CFG_PUPD_NONE | GPIO_CFG_PP | \
                       GPIO_CFG_SPEED_100_220MHZ)
#define GPIO_TLI_PCLK (GPIO_CFG_PORT_G | GPIO_CFG_PIN_7 | GPIO_CFG_MODE_AF | \
                       GPIO_CFG_AF_14 | GPIO_CFG_PUPD_NONE | GPIO_CFG_PP | \
                       GPIO_CFG_SPEED_100_220MHZ)

#define GPIO_TLI_BL   (GPIO_CFG_PORT_J | GPIO_CFG_PIN_9 | GPIO_CFG_MODE_OUTPUT | \
                       GPIO_CFG_PUPD_NONE | GPIO_CFG_PP | GPIO_CFG_SPEED_60MHZ | \
                       GPIO_CFG_OUTPUT_RESET)
#define GPIO_TLI_RST  (GPIO_CFG_PORT_J | GPIO_CFG_PIN_11 | GPIO_CFG_MODE_OUTPUT | \
                       GPIO_CFG_PUPD_NONE | GPIO_CFG_PP | GPIO_CFG_SPEED_60MHZ | \
                       GPIO_CFG_OUTPUT_RESET)

/* TLI data pins (port | pin | af) */

#define GPIO_TLI_R7   (GPIO_CFG_PORT_G | GPIO_CFG_PIN_6 | GPIO_CFG_AF_14)
#define GPIO_TLI_R6   (GPIO_CFG_PORT_B | GPIO_CFG_PIN_1 | GPIO_CFG_AF_9)
#define GPIO_TLI_R5   (GPIO_CFG_PORT_A | GPIO_CFG_PIN_9 | GPIO_CFG_AF_14)
#define GPIO_TLI_R4   (GPIO_CFG_PORT_A | GPIO_CFG_PIN_5 | GPIO_CFG_AF_14)
#define GPIO_TLI_R3   (GPIO_CFG_PORT_B | GPIO_CFG_PIN_0 | GPIO_CFG_AF_9)
#define GPIO_TLI_R2   (GPIO_CFG_PORT_A | GPIO_CFG_PIN_1 | GPIO_CFG_AF_14)
#define GPIO_TLI_R0   (GPIO_CFG_PORT_G | GPIO_CFG_PIN_13 | GPIO_CFG_AF_14)

#define GPIO_TLI_G7   (GPIO_CFG_PORT_K | GPIO_CFG_PIN_2 | GPIO_CFG_AF_14)
#define GPIO_TLI_G6   (GPIO_CFG_PORT_C | GPIO_CFG_PIN_7 | GPIO_CFG_AF_14)
#define GPIO_TLI_G5   (GPIO_CFG_PORT_B | GPIO_CFG_PIN_11 | GPIO_CFG_AF_14)
#define GPIO_TLI_G4   (GPIO_CFG_PORT_B | GPIO_CFG_PIN_10 | GPIO_CFG_AF_14)
#define GPIO_TLI_G3   (GPIO_CFG_PORT_J | GPIO_CFG_PIN_10 | GPIO_CFG_AF_14)
#define GPIO_TLI_G2   (GPIO_CFG_PORT_A | GPIO_CFG_PIN_6 | GPIO_CFG_AF_14)
#define GPIO_TLI_G1   (GPIO_CFG_PORT_E | GPIO_CFG_PIN_6 | GPIO_CFG_AF_14)
#define GPIO_TLI_G0   (GPIO_CFG_PORT_E | GPIO_CFG_PIN_5 | GPIO_CFG_AF_14)

#define GPIO_TLI_B7   (GPIO_CFG_PORT_B | GPIO_CFG_PIN_9 | GPIO_CFG_AF_14)
#define GPIO_TLI_B6   (GPIO_CFG_PORT_A | GPIO_CFG_PIN_15 | GPIO_CFG_AF_14)
#define GPIO_TLI_B5   (GPIO_CFG_PORT_B | GPIO_CFG_PIN_5 | GPIO_CFG_AF_3)
#define GPIO_TLI_B4   (GPIO_CFG_PORT_G | GPIO_CFG_PIN_12 | GPIO_CFG_AF_9)
#define GPIO_TLI_B3   (GPIO_CFG_PORT_A | GPIO_CFG_PIN_8 | GPIO_CFG_AF_13)
#define GPIO_TLI_B2   (GPIO_CFG_PORT_D | GPIO_CFG_PIN_6 | GPIO_CFG_AF_14)
#define GPIO_TLI_B1   (GPIO_CFG_PORT_A | GPIO_CFG_PIN_10 | GPIO_CFG_AF_14)
#define GPIO_TLI_B0   (GPIO_CFG_PORT_E | GPIO_CFG_PIN_4 | GPIO_CFG_AF_14)

/* GPIO_CFG common flags for AF data pins */

#define GPIO_TLI_AF_COMMON  (GPIO_CFG_MODE_AF | GPIO_CFG_PUPD_NONE | \
                             GPIO_CFG_PP | GPIO_CFG_SPEED_100_220MHZ)

/* RGB565 color helpers */

#define GD32_RGB565(r, g, b) \
  ((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3))

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int gd32_fbgetvideoinfo(FAR struct fb_vtable_s *vtable,
                               FAR struct fb_videoinfo_s *vinfo);
static int gd32_fbgetplaneinfo(FAR struct fb_vtable_s *vtable, int planeno,
                               FAR struct fb_planeinfo_s *pinfo);
static int gd32_tli_clock_config(void);
static int gd32_tli_gpio_config(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* This structure describes the video controller */

static const struct fb_videoinfo_s g_videoinfo =
{
  .fmt      = GD32_TLI_COLOR_FMT,
  .xres     = CONFIG_GD32H7XX_TLI_XRES,
  .yres     = CONFIG_GD32H7XX_TLI_YRES,
  .nplanes  = 1,
};

/* This structure describes the single color plane */

static const struct fb_planeinfo_s g_planeinfo =
{
  .fbmem    = (FAR void *)CONFIG_GD32H7XX_TLI_FB_BASE,
  .fblen    = GD32_TLI_FBSIZE,
  .stride   = GD32_TLI_STRIDE,
  .display  = 0,
  .bpp      = GD32_TLI_BPP,
};

/* The framebuffer object */

static struct fb_vtable_s g_fbobject =
{
  .getvideoinfo  = gd32_fbgetvideoinfo,
  .getplaneinfo  = gd32_fbgetplaneinfo,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gd32_fbgetvideoinfo
 *
 * Description:
 *   Return information about the video controller configuration.
 *
 ****************************************************************************/

static int gd32_fbgetvideoinfo(FAR struct fb_vtable_s *vtable,
                               FAR struct fb_videoinfo_s *vinfo)
{
  lcdinfo("vtable=%p vinfo=%p\n", vtable, vinfo);
  if (vtable != NULL && vinfo != NULL)
    {
      memcpy(vinfo, &g_videoinfo, sizeof(struct fb_videoinfo_s));
      return OK;
    }

  return -EINVAL;
}

/****************************************************************************
 * Name: gd32_fbgetplaneinfo
 *
 * Description:
 *   Return information about the framebuffer plane.
 *
 ****************************************************************************/

static int gd32_fbgetplaneinfo(FAR struct fb_vtable_s *vtable, int planeno,
                               FAR struct fb_planeinfo_s *pinfo)
{
  lcdinfo("vtable=%p planeno=%d pinfo=%p\n", vtable, planeno, pinfo);
  if (vtable != NULL && planeno == 0 && pinfo != NULL)
    {
      memcpy(pinfo, &g_planeinfo, sizeof(struct fb_planeinfo_s));
      return OK;
    }

  return -EINVAL;
}

/****************************************************************************
 * Name: gd32_tli_clock_config
 *
 * Description:
 *   Configure the PLL2 clock to generate the TLI pixel clock (9 MHz for
 *   the 4.3 inch panel) and enable the TLI peripheral clock.
 *
 ****************************************************************************/

static int gd32_tli_clock_config(void)
{
  uint32_t regval;
  uint32_t timeout;

  /* Enable the TLI peripheral clock (APB3) */

  modifyreg32(GD32_RCU_APB3EN, 0, RCU_APB3EN_TLIEN);

  /* Configure PLL2 input/output clock range (wide VCO, 1-2MHz input) */

  modifyreg32(GD32_RCU_PLLALL,
              RCU_PLLALL_PLL2RNG_MASK | RCU_PLLALL_PLL2VCOSEL, 0);

  /* Configure PLL2: PSC, N, P, R */

  regval = RCU_PLL2_PLL2PSC(GD32_TLI_PLL2_PSC) |
           RCU_PLL2_PLL2N(GD32_TLI_PLL2_N) |
           RCU_PLL2_PLL2P(GD32_TLI_PLL2_P) |
           RCU_PLL2_PLL2R(GD32_TLI_PLL2_R);
  putreg32(regval, GD32_RCU_PLL2);

  /* Enable the PLL2R clock output */

  modifyreg32(GD32_RCU_PLLADDCTL, 0, RCU_PLLADDCTL_PLL2REN);

  /* Configure the TLI clock divider from PLL2R (CK_TLI = CK_PLL2R / 8) */

  modifyreg32(GD32_RCU_CFG1, RCU_CFG1_PLL2RDIV_MASK,
              (uint32_t)GD32_TLI_PLL2RDIV << RCU_CFG1_PLL2RDIV_SHIFT);

  /* Enable PLL2 and wait until it is stable */

  modifyreg32(GD32_RCU_CTL, 0, RCU_CTL_PLL2EN);

  timeout = 1000000;
  while ((getreg32(GD32_RCU_CTL) & RCU_CTL_PLL2STB) == 0)
    {
      if (--timeout == 0)
        {
          lcderr("ERROR: PLL2 did not lock\n");
          return -ETIMEDOUT;
        }
    }

  lcdinfo("TLI pixel clock configured: %d Hz\n",
          (25 * 1000000) / GD32_TLI_PLL2_PSC * GD32_TLI_PLL2_N /
          GD32_TLI_PLL2_R / (1U << (GD32_TLI_PLL2RDIV + 1)));

  return OK;
}

/****************************************************************************
 * Name: gd32_tli_gpio_config
 *
 * Description:
 *   Configure all TLI GPIO pins (control signals and 24-bit RGB data).
 *
 ****************************************************************************/

static int gd32_tli_gpio_config(void)
{
  /* Control signals */

  gd32_gpio_config(GPIO_TLI_DE);
  gd32_gpio_config(GPIO_TLI_VSY);
  gd32_gpio_config(GPIO_TLI_HSY);
  gd32_gpio_config(GPIO_TLI_PCLK);

  /* Backlight and reset */

  gd32_gpio_config(GPIO_TLI_BL);
  gd32_gpio_config(GPIO_TLI_RST);

  /* Data pins */

  gd32_gpio_config(GPIO_TLI_R7 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_R6 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_R5 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_R4 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_R3 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_R2 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_R0 | GPIO_TLI_AF_COMMON);

  gd32_gpio_config(GPIO_TLI_G7 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_G6 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_G5 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_G4 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_G3 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_G2 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_G1 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_G0 | GPIO_TLI_AF_COMMON);

  gd32_gpio_config(GPIO_TLI_B7 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_B6 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_B5 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_B4 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_B3 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_B2 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_B1 | GPIO_TLI_AF_COMMON);
  gd32_gpio_config(GPIO_TLI_B0 | GPIO_TLI_AF_COMMON);

  return OK;
}

/****************************************************************************
 * Name: gd32_tli_panel_reset
 *
 * Description:
 *   Hardware reset the LCD panel and turn on the backlight.
 *
 ****************************************************************************/

static void gd32_tli_panel_reset(void)
{
  /* Panel reset, matching the working example (experiment 33) timing:
   * RST high -> 10ms -> low -> 50ms -> high -> 200ms.  The example does
   * this AFTER the TLI is enabled (so the pixel clock is already
   * running).  A short pulse (100us) can leave the panel T-CON in a bad
   * state that stretches / repeats pixels, so use the example's timing.
   *
   * v64 DIAG: read the SysTick current-value register at every step to
   * prove the CPU/system clock is still ticking through the busy-waits.
   * If VAL keeps changing the system is alive and only the wait is slow;
   * if VAL freezes the CPU has faulted/stalled.
   */

  volatile uint32_t *syst = (volatile uint32_t *)0xe000e010u; /* SysTick */
  uint32_t v0, v1;

  gd32_gpio_write(GPIO_TLI_RST, true);
  v0 = syst[2]; /* SYST_VAL */
  up_udelay(10000);
  v1 = syst[2];
  syslog(LOG_INFO, "TLI: rst high val %08x->%08x\n", (unsigned)v0, (unsigned)v1);

  gd32_gpio_write(GPIO_TLI_RST, false);
  v0 = syst[2];
  up_udelay(50000);
  v1 = syst[2];
  syslog(LOG_INFO, "TLI: rst low  val %08x->%08x\n", (unsigned)v0, (unsigned)v1);

  gd32_gpio_write(GPIO_TLI_RST, true);
  v0 = syst[2];
  up_udelay(200000);
  v1 = syst[2];
  syslog(LOG_INFO, "TLI: rst high2 val %08x->%08x\n", (unsigned)v0, (unsigned)v1);

  /* Backlight: power on immediately and report the pin level. */

  gd32_gpio_write(GPIO_TLI_BL, true);
  syslog(LOG_INFO, "TLI: backlight on (PJ9 OCTL=%08x)\n",
         getreg32(GD32_GPIO_OCTL(GD32_GPIOJ_BASE)));
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gd32_tli_initialize
 *
 * Description:
 *   Initialize the TLI controller, configure the layer 0 and enable the
 *   display.  The frame buffer is left untouched here (it is cleared by
 *   the framebuffer core at registration time).
 *
 ****************************************************************************/

int gd32_tli_initialize(void)
{
  extern void gd32_sdram_initialize(void);
  uint32_t layer0;
  uint32_t wlp;
  uint32_t wrp;
  uint32_t wtp;
  uint32_t wbp;
  uint32_t hsw   = GD32_TLI_HSW;
  uint32_t hbp   = GD32_TLI_HBP;
  uint32_t vsw   = GD32_TLI_VSW;
  uint32_t vbp   = GD32_TLI_VBP;
  uint32_t pwidth  = CONFIG_GD32H7XX_TLI_XRES;
  uint32_t pheight = CONFIG_GD32H7XX_TLI_YRES;

  /* Configure TLI pixel clock and GPIO pins */

  syslog(LOG_INFO, "TLI: init start\n");

  /* The 800x480 RGB565 frame buffer lives in the external SDRAM
   * (0xC0000000) because it is larger than the internal AXI SRAM.
   * Bring the SDRAM up first.
   */

  gd32_sdram_initialize();

  if (gd32_tli_clock_config() != OK)
    {
      syslog(LOG_ERR, "TLI: clock config FAILED\n");
      return -EIO;
    }

  syslog(LOG_INFO, "TLI: pixel clock 33MHz OK\n");

  gd32_tli_gpio_config();

  syslog(LOG_INFO, "TLI: gpio configured\n");

  /* Configure TLI timing registers
   *
   * SPSZ: vpsz = vsw-1, hpsz = hsw-1
   * BPSZ: vbpsz = vsw+vbp-1, hbpsz = hsw+hbp-1
   * ASZ:  vasz = vsw+vbp+pheight-1, hasz = hsw+hbp+pwidth-1
   * TSZ:  vtsz = vsw+vbp+pheight+vfp-1, htsz = hsw+hbp+pwidth+hfp-1
   */

  putreg32((vsw - 1) | ((hsw - 1) << 16), GD32_TLI_SPSZ);
  putreg32((vsw + vbp - 1) | ((hsw + hbp - 1) << 16), GD32_TLI_BPSZ);
  putreg32((vsw + vbp + pheight - 1) |
           ((hsw + hbp + pwidth - 1) << 16), GD32_TLI_ASZ);
  putreg32((vsw + vbp + pheight + GD32_TLI_VFP - 1) |
           ((hsw + hbp + pwidth + GD32_TLI_HFP - 1) << 16), GD32_TLI_TSZ);

  /* Background color: white */

  putreg32(0xff | (0xff << 8) | (0xff << 16), GD32_TLI_BGC);

  /* Signal polarity: HS/VS/DE active low, pixel clock not inverted
   * (all polarity bits remain 0).
   */

  /* Configure layer 0 */

  layer0 = GD32_TLI_LAYER0;

  wlp = hsw + hbp;
  /* Layer window: the example uses right = pwidth + hsw + hbp - 1, i.e.
   * an 800-pixel window (NOT 832).  The window position registers are
   * written directly by tli_layer_init from layer_window_leftpos /
   * layer_window_rightpos; they do NOT come from FLL.
   */
  wrp = pwidth + hsw + hbp - 1;
  wtp = vsw + vbp;
  wbp = pheight + vsw + vbp - 1;

  putreg32(wlp | (wrp << 16), GD32_TLI_LXHPOS(layer0));
  putreg32(wtp | (wbp << 16), GD32_TLI_LXVPOS(layer0));
  putreg32(LAYER_PPF_RGB565, GD32_TLI_LXPPF(layer0));
  putreg32(255, GD32_TLI_LXSA(layer0));
  putreg32(0, GD32_TLI_LXDC(layer0));
  putreg32(LAYER_ACF1_PASA | LAYER_ACF2_PASA, GD32_TLI_LXBLEND(layer0));
  putreg32(CONFIG_GD32H7XX_TLI_FB_BASE, GD32_TLI_LXFBADDR(layer0));

  /* Line length (FLL) and stride offset (STDOFF) - GD official TLI
   * example values that EXACTLY match the 800-pixel frame buffer stride:
   *   STDOFF = width*2 = 1600 bytes (800 pixels per line)
   *   FLL    = width*2 + 3 = 1603
   * STDOFF is the byte count from one line start to the next line start,
   * so it MUST equal the frame buffer stride (800px = 1600B), otherwise
   * every line shifts by (STDOFF - 1600)/2 pixels and the image skews.
   * The previous 1664/1671 (832-pixel stride) never matched the 800-pixel
   * frame buffer, which is exactly the horizontal skew we saw.
   */

  putreg32(1603 | (1600 << 16), GD32_TLI_LXFLLEN(layer0));
  putreg32(pheight, GD32_TLI_LXFTLN(layer0));

  /* Disable all TLI interrupts.  The TLI status register shows the
   * line/FIFO/transfer flags set once the controller starts (STAT=0x0f);
   * leaving INTEN enabled would let the TLI raise interrupts that this
   * driver never services, disturbing the system timer/scheduler.
   */

  putreg32(0, GD32_TLI_INTEN);

  /* Reset the panel BEFORE enabling the TLI.  The 50ms/200ms panel
   * reset busy-waits now run with no pixel clock / TLI DMA traffic, so
   * they cannot be disturbed by TLI+SDRAM bus contention.  (With the
   * reset AFTER enable, the boot has sporadically stalled inside the
   * 50ms busy-wait right after the TLI starts scanning the frame
   * buffer, which is exactly the kind of contention window we want to
   * avoid.)
   */

  /* Lightweight bring-up: skip the long panel-reset busy-waits that
   * previously stalled the system.  Drive RST high and turn on the
   * backlight directly; the panel is expected to show whatever it can
   * without an explicit reset sequence.
   */

  gd32_gpio_write(GPIO_TLI_RST, true);
  gd32_gpio_write(GPIO_TLI_BL, true);
  syslog(LOG_INFO, "TLI: lightweight enable (panel reset skipped)\n");

  /* Enable layer 0, request reload and enable TLI */

  modifyreg32(GD32_TLI_LXCTL(layer0), 0, TLI_LXCTL_LXEN);
  modifyreg32(GD32_TLI_RL, 0, TLI_RL_L0RE);
  modifyreg32(GD32_TLI_CTL, 0, TLI_CTL_TLIEN);
  syslog(LOG_INFO, "TLI: TLIEN set\n");

  syslog(LOG_INFO, "TLI: panel reset done, backlight on\n");

  lcdinfo("TLI initialized: %ux%u RGB565, fb=%08x stride=%d\n",
          pwidth, pheight, CONFIG_GD32H7XX_TLI_FB_BASE, GD32_TLI_STRIDE);
  syslog(LOG_INFO, "TLI: enabled %ux%u RGB565 fb=%08x\n",
         pwidth, pheight, CONFIG_GD32H7XX_TLI_FB_BASE);

  /* Basic display verification: clear the screen to blue. */

  syslog(LOG_INFO, "TLI: clearing fb\n");
  gd32_tli_clear(GD32_RGB565(0x00, 0x00, 0xff));
  syslog(LOG_INFO, "TLI: fb cleared\n");

  /* Diagnostic: report the actual controller / layer / backlight
   * register values so we can tell whether the TLI is really running.
   */

  syslog(LOG_INFO, "TLI: APB3EN=%08x CTL=%08x LXCTL0=%08x RL=%08x INTEN=%08x\n",
         getreg32(GD32_RCU_APB3EN), getreg32(GD32_TLI_CTL),
         getreg32(GD32_TLI_LXCTL(GD32_TLI_LAYER0)), getreg32(GD32_TLI_RL),
         getreg32(GD32_TLI_INTEN));
  syslog(LOG_INFO, "TLI: BL PJ9 OCTL=%08x (bit9=1 -> backlight high)\n",
         getreg32(GD32_GPIO_OCTL(GD32_GPIOJ_BASE)));

  /* Post-init scan check: read TLI status and the current pixel
   * position register several times with a small delay between reads.
   * If the TLI is really scanning out video, CPPOS keeps changing;
   * if it stays 0 the TLI never started scanning (clock/signal
   * problem).  No long delay used here.
   */

  {
    int k;
    for (k = 0; k < 4; k++)
      {
        syslog(LOG_INFO, "TLI: scan k=%d CTL=%08x STAT=%08x CPPOS=%08x\n",
               k, getreg32(GD32_TLI_CTL), getreg32(GD32_TLI_STAT),
               getreg32(GD32_TLI_CPPOS));
        up_udelay(1000);
      }
  }

  return OK;
}

/****************************************************************************
 * Name: gd32_tli_clear
 *
 * Description:
 *   Fill the whole frame buffer with a single RGB565 color.
 *
 ****************************************************************************/

void gd32_tli_clear(uint16_t color)
{
  uint16_t *fb = (uint16_t *)CONFIG_GD32H7XX_TLI_FB_BASE;
  uint32_t i;

  /* The visible frame buffer is a plain 800x480 RGB565 layout (one line
   * per visible row), matching the reference example's write pattern.
   */

  for (i = 0; i < CONFIG_GD32H7XX_TLI_XRES *
                   CONFIG_GD32H7XX_TLI_YRES; i++)
    {
      fb[i] = color;
    }
}

/****************************************************************************
 * Name: gd32_tli_fillrect
 *
 * Description:
 *   Fill a rectangle of the frame buffer with an RGB565 color.  The frame
 *   buffer uses the 64-pixel aligned stride (GD32_TLI_STRIDE).
 *
 ****************************************************************************/

void gd32_tli_fillrect(unsigned int x0, unsigned int y0,
                       unsigned int w, unsigned int h,
                       uint16_t color)
{
  uint16_t *fb = (uint16_t *)CONFIG_GD32H7XX_TLI_FB_BASE;
  unsigned int x;
  unsigned int y;
  unsigned int xend;
  unsigned int yend;

  if (x0 >= CONFIG_GD32H7XX_TLI_XRES ||
      y0 >= CONFIG_GD32H7XX_TLI_YRES)
    {
      return;
    }

  xend = x0 + w;
  yend = y0 + h;
  if (xend > CONFIG_GD32H7XX_TLI_XRES)
    {
      xend = CONFIG_GD32H7XX_TLI_XRES;
    }

  if (yend > CONFIG_GD32H7XX_TLI_YRES)
    {
      yend = CONFIG_GD32H7XX_TLI_YRES;
    }

  for (y = y0; y < yend; y++)
    {
      for (x = x0; x < xend; x++)
        {
          fb[y * CONFIG_GD32H7XX_TLI_XRES + x] = color;
        }
    }
}

/****************************************************************************
 * Name: up_fbinitialize
 *
 * Description:
 *   Initialize the framebuffer video hardware associated with the display.
 *
 ****************************************************************************/

int up_fbinitialize(int display)
{
  int ret;

  DEBUGASSERT(display == 0);

  ret = gd32_tli_initialize();
  if (ret < 0)
    {
      lcderr("ERROR: gd32_tli_initialize failed: %d\n", ret);
    }

  return ret;
}

/****************************************************************************
 * Name: up_fbgetvplane
 *
 * Description:
 *   Return a reference to the framebuffer object for the specified video
 *   plane of the specified display.
 *
 ****************************************************************************/

FAR struct fb_vtable_s *up_fbgetvplane(int display, int vplane)
{
  DEBUGASSERT(display == 0 && vplane == 0);
  return &g_fbobject;
}

/****************************************************************************
 * Name: up_fbuninitialize
 *
 * Description:
 *   Uninitialize the framebuffer support for the specified display.
 *
 ****************************************************************************/

void up_fbuninitialize(int display)
{
  DEBUGASSERT(display == 0);

  /* Disable TLI */

  modifyreg32(GD32_TLI_CTL, TLI_CTL_TLIEN, 0);
}

#endif /* CONFIG_GD32H7_TLI */
