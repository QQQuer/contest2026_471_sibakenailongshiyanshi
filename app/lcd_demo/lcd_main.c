/****************************************************************************
 * apps/examples/lcd/lcd_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Runtime LCD enable demo (no-LCD boot, on-demand display).
 *
 * The board boots without initialising the TLI/LCD.  Running the 'lcd'
 * command performs the full TLI bring-up (SDRAM, pixel clock, GPIO,
 * timing, layer 0, panel reset) via the board service board_lcd_enable()
 * and draws a simple test pattern.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

/* Board service exported from gd32h759imt6 board support */

extern int board_lcd_enable(void);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  int ret;

  ret = board_lcd_enable();
  if (ret != OK)
    {
      fprintf(stderr, "LCD: enable failed: %d\n", ret);
      return EXIT_FAILURE;
    }

  printf("LCD: enabled\n");
  return EXIT_SUCCESS;
}
