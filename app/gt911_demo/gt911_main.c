/****************************************************************************
 * apps/examples/gt911/gt911_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * GT911 touch demo - TOUCH-ONLY mode (no LCD/TLI).
 *
 * The TLI/LCD display is disabled in this build (CONFIG_GD32H7_TLI=n).
 * This app only initialises the GT911 controller over the soft-I2C and
 * reports touch coordinates on the serial console.  It never touches the
 * SDRAM frame buffer (0xC0000000), which is not initialised without the
 * TLI driver.
 *
 * Press any key on the console to exit.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/select.h>

/* Board lower-half services exported from the GD32H759IMT6 board support */

extern int gt911_lower_init(void);
extern int gt911_lower_scan(FAR int *x, FAR int *y, FAR int *down);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool gt911_anykey(void)
{
  fd_set rfds;
  struct timeval tv;
  int ret;

  FD_ZERO(&rfds);
  FD_SET(STDIN_FILENO, &rfds);
  tv.tv_sec  = 0;
  tv.tv_usec = 0;

  ret = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
  return (ret > 0);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  int x = 0;
  int y = 0;
  int down = 0;
  int ret;

  ret = gt911_lower_init();
  if (ret != OK)
    {
      fprintf(stderr, "GT911: init failed: %d\n", ret);
      return EXIT_FAILURE;
    }

  printf("GT911: init OK, touch the screen (any key to stop)\n");

  while (1)
    {
      ret = gt911_lower_scan(&x, &y, &down);
      if (ret == OK && down)
        {
          printf("Touch: (%d,%d)\n", x, y);
        }

      usleep(20000);

      if (gt911_anykey())
        {
          break;
        }
    }

  printf("GT911: demo stopped\n");
  return EXIT_SUCCESS;
}
