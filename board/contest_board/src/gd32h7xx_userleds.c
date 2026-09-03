/****************************************************************************
 * vendor/gigadevice/boards/gd32h7/gd32h759imt6/src/gd32h7xx_userleds.c
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

#include <stdbool.h>
#include <debug.h>

#include <sys/param.h>

#include <nuttx/board.h>
#include <arch/board/board.h>

#include "gd32h7xx_gpio.h"
#include "gd32h759imt6.h"
#include "hardware/gd32h7xx_gpio.h"
#include "hardware/gd32h7xx_rcu.h"

#ifndef CONFIG_ARCH_LEDS

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* LED index. The GD32H759IMT6 Core Board has two user LEDs:
 *   LED0 (red)   - PC13
 *   LED1 (green) - PJ8
 */

static const uint32_t g_led_map[BOARD_LEDS] =
{
  LED0,
  LED1
};

static const uint32_t g_led_setmap[BOARD_LEDS] =
{
  BOARD_LED1_BIT,
  BOARD_LED2_BIT
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Turn on selected led */

void gd32_eval_led_on(led_typedef_enum led_num)
{
  /* LEDs are active-low (resistor to VCC3.3, GPIO low = LED on) */

  gd32_gpio_write(g_led_map[led_num], false);
}

/* Turn off selected led */

void gd32_eval_led_off(led_typedef_enum led_num)
{
  gd32_gpio_write(g_led_map[led_num], true);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_userled_initialize
 *
 * Description:
 *   If CONFIG_ARCH_LEDS is defined, then NuttX will control the on-board
 *   LEDs.  If CONFIG_ARCH_LEDS is not defined, then the
 *   board_userled_initialize() is available to initialize the LED from user
 *   application logic.
 *
 ****************************************************************************/

uint32_t board_userled_initialize(void)
{
  int i;

  /* Configure the LED GPIO for output. */

  for (i = 0; i < nitems(g_led_map); i++)
    {
      int rc = gd32_gpio_config(g_led_map[i]);
      _alert("ULED_INIT: led=%d cfg=0x%08x rc=%d\n", i,
             (unsigned)g_led_map[i], rc);
    }

  /* Dump actual GPIO register state after config */

  {
#define URD(addr) (*(volatile uint32_t *)(addr))
    uint32_t rcu = GD32_RCU_BASE + GD32_RCU_AHB4EN_OFFSET;
    uint32_t pj  = GD32_GPIOJ_BASE;
    uint32_t ph  = GD32_GPIOH_BASE;
    uint32_t pc  = GD32_GPIOC_BASE;

    _alert("ULED_RCU: AHB4EN@0x%08x = %08x (PJEN bit8=%d, PCEN bit2=%d)\n",
           (unsigned)rcu, URD(rcu),
           (int)((URD(rcu) >> 8) & 1),
           (int)((URD(rcu) >> 2) & 1));
    _alert("ULED_GPIOC: CTL=%08x OCTL=%08x\n",
           URD(GD32_GPIO_CTL(pc)), URD(GD32_GPIO_OCTL(pc)));
    _alert("ULED_GPIOH: CTL=%08x OCTL=%08x\n",
           URD(GD32_GPIO_CTL(ph)), URD(GD32_GPIO_OCTL(ph)));
    _alert("ULED_GPIOJ: CTL=%08x PUD=%08x OMODE=%08x OSPD=%08x OCTL=%08x\n",
           URD(GD32_GPIO_CTL(pj)),
           URD(GD32_GPIO_PUD(pj)),
           URD(GD32_GPIO_OMODE(pj)),
           URD(GD32_GPIO_OSPD(pj)),
           URD(GD32_GPIO_OCTL(pj)));
#undef URD
  }

  return BOARD_LEDS;
}

/****************************************************************************
 * Name: board_userled
 *
 * Description:
 *   If CONFIG_ARCH_LEDS is defined, then NuttX will control the on-board
 *   LEDs.  If CONFIG_ARCH_LEDS is not defined, then the board_userled() is
 *   available to control the LED from user application logic.
 *
 ****************************************************************************/

void board_userled(int led, bool ledon)
{
  /* LEDs are active-low: ledon=true means LED lit = GPIO low */

  if ((unsigned)led < nitems(g_led_map))
    {
      gd32_gpio_write(g_led_map[led], !ledon);
    }
}

/****************************************************************************
 * Name: board_userled_all
 *
 * Description:
 *   If CONFIG_ARCH_LEDS is defined, then NuttX will control the on-board
 *   LEDs.  If CONFIG_ARCH_LEDS is not defined, then the board_userled_all()
 *   is available to control the LED from user application logic.
 *
 ****************************************************************************/

void board_userled_all(uint32_t ledset)
{
  int i;

  /* Configure LED GPIOs for output */

  for (i = 0; i < nitems(g_led_map); i++)
    {
      bool lit = (ledset & g_led_setmap[i]) != 0;

      /* active-low: lit -> GPIO low */

      gd32_gpio_write(g_led_map[i], !lit);
    }
}

#endif /* !CONFIG_ARCH_LEDS */
