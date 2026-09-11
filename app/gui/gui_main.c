/****************************************************************************
 * gui_main.c - OpenVela contest demo GUI
 *
 * Pages (entered from the main-page buttons, or from the serial console):
 *   0 main     : WQY title (top-centre) + 3 entry buttons
 *   1 touch    : finger drawing + live coordinate (x:123, y:123) top-right
 *   3 measure  : ADC + FFT spectrum analyser (time domain + magnitude)
 *   4 sysinfo  : system information (incl. screen model)
 *
 * Input: GT911 touch (board lower-half).  Serial fallback: keys 1/2/3
 * enter pages 1/3/4, b returns to the main page, q quits.
 *
 * All drawing writes straight into the RGB565 frame buffer in SDRAM at
 * 0xC0000000 (the TLI driver scans it out); there is no NX / graphics
 * library involved.
 *
 * The handwriting page (page 2) and its 16x16 grid recogniser - stroke
 * bbox rasterisation matched against 10 pre-rendered WQY digit templates
 * (ncr_templates.h) by pixel coverage - are still compiled in but no
 * longer reachable: the v54 layout dropped the entry button because
 * recognition proved unreliable in practice.  draw_hand_page(), the ncr_*
 * helpers and their state are kept for reference.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>

#include "gui_title_bitmap.h"
#include "gui_text.h"
#include "gui_digits.h"
#include "ncr_templates.h"

/* board services (FLAT build) */
extern int  board_lcd_enable(void);
extern int  gt911_lower_init(void);
extern int  gt911_lower_scan(FAR int *x, FAR int *y, FAR int *down);

#define LCD_W 800
#define LCD_H 480

#define C_WHITE   0xffff
#define C_BLACK   0x0000
#define C_GREY    0x8c9aa8
#define C_BORDER  0x5b96
#define C_HILITE  0xdf1e    /* light blue pressed fill (RGB565) */

/* ------------------------------------------------------------------ */
/* framebuffer primitives                                              */
/* ------------------------------------------------------------------ */

static void fb_fill(int x, int y, int w, int h, uint16_t c)
{
  volatile uint16_t *fb = (volatile uint16_t *)0xc0000000;
  int yy, xx;

  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_W) { w = LCD_W - x; }
  if (y + h > LCD_H) { h = LCD_H - y; }
  if (w <= 0 || h <= 0) { return; }

  for (yy = 0; yy < h; yy++)
    {
      uint16_t *row = (uint16_t *)fb + (y + yy) * LCD_W + x;
      for (xx = 0; xx < w; xx++) { row[xx] = c; }
    }
}

static void fb_px(int x, int y, uint16_t c)
{
  if (x >= 0 && x < LCD_W && y >= 0 && y < LCD_H)
    {
      ((volatile uint16_t *)0xc0000000)[y * LCD_W + x] = c;
    }
}

/* ------------------------------------------------------------------ */
/* gray4 bitmap helpers                                                */
/* ------------------------------------------------------------------ */

static void draw_gray4(int x, int y, int w, int h, const uint8_t *bits,
                       int scale)
{
  volatile uint16_t *fb = (volatile uint16_t *)0xc0000000;
  int yy, xx, dy, dx;

  for (yy = 0; yy < h; yy++)
    {
      for (xx = 0; xx < w; xx++)
        {
          int b = bits[yy * (w / 2) + (xx >> 1)];
          int v = (xx & 1) ? (b & 0x0f) : (b >> 4);

          if (v == 0) { continue; }

          for (dy = 0; dy < scale; dy++)
            {
              int sy = y + yy * scale + dy;
              if (sy < 0 || sy >= LCD_H) { continue; }
              for (dx = 0; dx < scale; dx++)
                {
                  int sx = x + xx * scale + dx;
                  if (sx < 0 || sx >= LCD_W) { continue; }
                  {
                    /* v41: linear blackness -> RGB565.  v=15 (pure black
                     * glyph pixel) now maps to 0x0000.
                     */
                    int br = 15 - v;              /* 0..15 brightness */
                    int r5 = (br * 31 + 7) / 15;  /* 0..31 */
                    int g6 = (br * 63 + 7) / 15;  /* 0..63 */
                    int b5 = (br * 31 + 7) / 15;  /* 0..31 */
                    fb[sy * LCD_W + sx] = (uint16_t)((r5 << 11) |
                                                     (g6 << 5) | b5);
                  }
                }
            }
        }
    }
}

static void blit_text(int x, int y, int w, int h, const uint8_t *bits,
                      int scale)
{
  draw_gray4(x, y, w, h, bits, scale);
}

static void blit_center(int y, int w, int h, const uint8_t *bits, int scale)
{
  draw_gray4((LCD_W - w * scale) / 2, y, w, h, bits, scale);
}

static void draw_gray4_fg(int x, int y, int w, int h, const uint8_t *bits,
                          int scale, uint16_t fg)
{
  volatile uint16_t *fb = (volatile uint16_t *)0xc0000000;
  int yy, xx, dy, dx;

  for (yy = 0; yy < h; yy++)
    {
      for (xx = 0; xx < w; xx++)
        {
          int b = bits[yy * (w / 2) + (xx >> 1)];
          int v = (xx & 1) ? (b & 0x0f) : (b >> 4);

          if (v == 0) { continue; }

          for (dy = 0; dy < scale; dy++)
            {
              int sy = y + yy * scale + dy;
              if (sy < 0 || sy >= LCD_H) { continue; }
              for (dx = 0; dx < scale; dx++)
                {
                  int sx = x + xx * scale + dx;
                  if (sx < 0 || sx >= LCD_W) { continue; }
                  fb[sy * LCD_W + sx] = fg;
                }
            }
        }
    }
}

static void blit_digits(int x, int y, const char *str, uint16_t fg)
{
  int i, cx = x;

  for (i = 0; str[i] != '\0'; i++)
    {
      const char *p = strchr(GUI_DIG_CHARS, str[i]);
      int idx;

      if (p == NULL) { continue; }
      idx = (int)(p - GUI_DIG_CHARS);
      draw_gray4_fg(cx, y, gui_dig_w[idx], GUI_DIG_H,
                    gui_dig_bits[idx], 1, fg);
      cx += gui_dig_w[idx] + 2;
    }
}

/* ------------------------------------------------------------------ */
/* shapes                                                              */
/* ------------------------------------------------------------------ */

static void draw_rect(int x, int y, int w, int h, uint16_t c)
{
  fb_fill(x, y, w, 2, c);
  fb_fill(x, y + h - 2, w, 2, c);
  fb_fill(x, y, 2, h, c);
  fb_fill(x + w - 2, y, 2, h, c);
}

static void draw_circle(int cx, int cy, int r, uint16_t c)
{
  int x, y;

  for (y = -r; y <= r; y++)
    {
      for (x = -r; x <= r; x++)
        {
          if (x * x + y * y <= r * r)
            {
              fb_px(cx + x, cy + y, c);
            }
        }
    }
}

static void draw_line(int x0, int y0, int x1, int y1, int w, uint16_t c)
{
  int dx = abs(x1 - x0);
  int dy = -abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  for (;;)
    {
      draw_circle(x0, y0, w / 2, c);
      if (x0 == x1 && y0 == y1) { break; }
      {
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
      }
    }
}

/* ------------------------------------------------------------------ */
/* dynamic digit text (WQY glyphs)                                     */
/* ------------------------------------------------------------------ */

static int digit_text_w(int scale, const char *s)
{
  int w = 0;

  for (; *s; s++)
    {
      const char *p = strchr(GUI_DIG_CHARS, *s);
      if (p)
        {
          w += gui_dig_w[p - GUI_DIG_CHARS] * scale;
        }
    }

  return w;
}

static void draw_digit_text(int x, int y, int scale, const char *s,
                            uint16_t color)
{
  int i;

  (void)color;

  for (i = 0; s[i]; i++)
    {
      const char *p = strchr(GUI_DIG_CHARS, s[i]);
      if (p)
        {
          int idx = p - GUI_DIG_CHARS;
          blit_text(x, y, gui_dig_w[idx], GUI_DIG_H,
                    gui_dig_bits[idx], scale);
          x += gui_dig_w[idx] * scale;
        }
    }
}

/* ------------------------------------------------------------------ */
/* page definitions                                                    */
/* ------------------------------------------------------------------ */

enum { PG_MAIN = 0, PG_TOUCH = 1, PG_HAND = 2, PG_MEAS = 3, PG_SYS = 4,
       PG_DIAG = 99 };

struct btn
{
  int x, y, w, h;
  int page;
  int txt_w, txt_h;
  const uint8_t *txt;
};

#define BTN_W 360
#define BTN_H 52
#define BTN_X ((LCD_W - BTN_W) / 2)
#define BTN_Y0 200
#define BTN_GAP 16

/* v54: the handwriting entry was dropped - three buttons only */
static const struct btn g_btns[3] =
{
  { BTN_X, BTN_Y0,                       BTN_W, BTN_H, PG_TOUCH,
    GUI_TXT_BTN1_W, GUI_TXT_BTN1_H, gui_txt_btn1_bits },
  { BTN_X, BTN_Y0 + (BTN_H + BTN_GAP),   BTN_W, BTN_H, PG_MEAS,
    GUI_TXT_BTN3_W, GUI_TXT_BTN3_H, gui_txt_btn3_bits },
  { BTN_X, BTN_Y0 + 2 * (BTN_H + BTN_GAP), BTN_W, BTN_H, PG_SYS,
    GUI_TXT_BTN4_W, GUI_TXT_BTN4_H, gui_txt_btn4_bits },
};

static const struct btn g_back =
{ 16, 14, 116, 38, PG_MAIN, GUI_TXT_BACK_W, GUI_TXT_BACK_H,
  gui_txt_back_bits };

/* handwriting page: draw area + clear button */
#define HAND_X 40
#define HAND_Y 110
#define HAND_W 520
#define HAND_H 320

#define CLEAR_X 600
#define CLEAR_Y 300
#define CLEAR_W 180
#define CLEAR_H 56

static int g_page = PG_MAIN;
static volatile bool g_run = true;
static int g_tx = -1;   /* last touch point (stroke) */
static int g_ty = -1;

/* 4-corner touch calibration (kept, unused: raw coords are native) */
enum { CAL_NONE = -1, CAL_TL = 0, CAL_TR = 1, CAL_BL = 2, CAL_BR = 3 };
static int g_cal = CAL_NONE;
static int g_cx[4], g_cy[4];
static bool g_cal_done = false;

/* 9-point calibration: screen points (3x3 grid) */
#define C9_PTS 9
#define C9_CX(n) (((n) % 3) * 400)
#define C9_CY(n) (((n) / 3) * 240)
static int g_p9[C9_PTS][2];
static int g_cal9 = 0;
static int g_lrx = -1, g_lry = -1;

struct tri
{
  int x0, y0;
  int vxx, vxy, vyx, vyy, det;
  int sx0, sy0;
};
static struct tri g_tri[8];
static int g_ccx[4], g_ccy[4];


/* ------------------------------------------------------------------ */
/* handwriting recognition (0-9, grid match)                           */
/* ------------------------------------------------------------------ */

#define NCR_MAX_PTS 1024

static int g_ncr_x[NCR_MAX_PTS];
static int g_ncr_y[NCR_MAX_PTS];
static int g_ncr_n = 0;

/* Idle-wait state machine (mirrors example 33): recognition runs only
 * after the finger has been up for NCR_IDLE_LOOPS main-loop iterations
 * (each loop sleeps 20 ms, so 20 loops ~= 400 ms of no writing).
 */
#define NCR_IDLE_LOOPS 20
static int g_ncr_idle = 0;

static void ncr_reset(void)
{
  g_ncr_n = 0;
  g_ncr_idle = 0;
}

/* A 16-bit coordinate is rebuilt from two bytes (xh<<8|xl style).  When
 * xh or yh carries, the decoded value jumps by ~256 - not a real finger
 * movement.  Drop any sample that jumps more than this many pixels.
 */
#define NCR_JUMP 260

static void ncr_add(int x, int y)
{
  if (g_ncr_n > 0)
    {
      int dx = x - g_ncr_x[g_ncr_n - 1];
      int dy = y - g_ncr_y[g_ncr_n - 1];

      if (dx * dx + dy * dy > NCR_JUMP * NCR_JUMP)
        {
          return;   /* carry-jump / glitch sample */
        }
    }

  if (g_ncr_n < NCR_MAX_PTS)
    {
      g_ncr_x[g_ncr_n] = x;
      g_ncr_y[g_ncr_n] = y;
      g_ncr_n++;
    }
}

static void ncr_rasterize(uint8_t grid[NCR_N][NCR_N])
{
  int minx, miny, maxx, maxy, i;
  int w, h, gw, gh, ox, oy;
  float s;
  uint8_t tmp[NCR_N][NCR_N];

  minx = maxx = g_ncr_x[0];
  miny = maxy = g_ncr_y[0];

  for (i = 1; i < g_ncr_n; i++)
    {
      if (g_ncr_x[i] < minx) { minx = g_ncr_x[i]; }
      if (g_ncr_x[i] > maxx) { maxx = g_ncr_x[i]; }
      if (g_ncr_y[i] < miny) { miny = g_ncr_y[i]; }
      if (g_ncr_y[i] > maxy) { maxy = g_ncr_y[i]; }
    }

  w = maxx - minx + 1;
  h = maxy - miny + 1;
  if (w < 1) { w = 1; }
  if (h < 1) { h = 1; }

  s = (float)(NCR_N - 2) / (w > h ? w : h);
  gw = (int)(w * s);
  gh = (int)(h * s);
  if (gw < 1) { gw = 1; }
  if (gh < 1) { gh = 1; }
  ox = (NCR_N - gw) / 2;
  oy = (NCR_N - gh) / 2;

  memset(grid, 0, NCR_N * NCR_N);

  for (i = 0; i < g_ncr_n; i++)
    {
      int gx = ox + (int)((g_ncr_x[i] - minx) * s);
      int gy = oy + (int)((g_ncr_y[i] - miny) * s);

      if (gx >= 0 && gx < NCR_N && gy >= 0 && gy < NCR_N)
        {
          grid[gy][gx] = 1;
        }
    }

  /* dilate once (8-neighbour) so thin strokes thicken up */
  memcpy(tmp, grid, sizeof(tmp));
  for (i = 0; i < NCR_N * NCR_N; i++)
    {
      int gx = i % NCR_N;
      int gy = i / NCR_N;
      int dx, dy;

      if (!tmp[gy][gx]) { continue; }

      for (dy = -1; dy <= 1; dy++)
        {
          for (dx = -1; dx <= 1; dx++)
            {
              int nx = gx + dx;
              int ny = gy + dy;

              if (nx >= 0 && nx < NCR_N && ny >= 0 && ny < NCR_N)
                {
                  grid[ny][nx] = 1;
                }
            }
        }
    }
}

static int ncr_match(const uint8_t grid[NCR_N][NCR_N], int tmpl)
{
  int best = 0;
  int dx, dy;

  /* tolerate +/-1 cell misalignment: the stroke may sit slightly off
   * centre in the 16x16 box (e.g. a '1' drawn at the box edge) */
  for (dy = -1; dy <= 1; dy++)
    {
      for (dx = -1; dx <= 1; dx++)
        {
          int hit = 0, tot = 0, thit = 0, ttot = 0;
          int x, y;

          for (y = 0; y < NCR_N; y++)
            {
              for (x = 0; x < NCR_N; x++)
                {
                  int gx = x - dx;
                  int gy = y - dy;
                  int g = (gx >= 0 && gx < NCR_N && gy >= 0 && gy < NCR_N) ?
                          grid[gy][gx] : 0;
                  int bit = (ncr_tmpl[tmpl][y * 2 + (x >> 3)] >>
                             (7 - (x & 7))) & 1;

                  if (g)
                    {
                      tot++;
                      if (bit) { hit++; }
                    }

                  if (bit)
                    {
                      ttot++;
                      if (g) { thit++; }
                    }
                }
            }

          if (tot > 0 && ttot > 0)
            {
              /* average of both coverage directions (F1-like) */
              int sc = (hit * 1000 / tot + thit * 1000 / ttot) / 2;

              if (sc > best) { best = sc; }
            }
        }
    }

  return best;
}

static int ncr_recognize(void)
{
  uint8_t grid[NCR_N][NCR_N];
  int best = -1, bestsc = 0;
  int t;

  if (g_ncr_n < 8)
    {
      return -1;
    }

  ncr_rasterize(grid);

  for (t = 0; t < 10; t++)
    {
      int sc = ncr_match(grid, t);

      if (sc > bestsc)
        {
          bestsc = sc;
          best = t;
        }
    }

  return (bestsc >= 250) ? best : -1;
}

/* Result panel on the right side of the handwriting area:
 *   +------------------+
 *   |    识别:           |
 *   |      5            |   (large digit, centred)
 *   +------------------+
 * Drawn on every recognition (finger lift) and on page enter/clear.
 */
#define NCR_RX 600
#define NCR_RY 110
#define NCR_RW 180
#define NCR_RH 150

static void ncr_show_result(int r)
{
  fb_fill(NCR_RX, NCR_RY, NCR_RW, NCR_RH, C_WHITE);
  draw_rect(NCR_RX, NCR_RY, NCR_RW, NCR_RH, C_BORDER);

  if (r < 0)
    {
      blit_text(NCR_RX + (NCR_RW - GUI_TXT_NCR_NONE_W) / 2, NCR_RY + 55,
                GUI_TXT_NCR_NONE_W, GUI_TXT_NCR_NONE_H,
                gui_txt_ncr_none_bits, 1);
    }
  else
    {
      char d[2] = { (char)('0' + r), '\0' };
      int dw = digit_text_w(3, d);

      blit_text(NCR_RX + (NCR_RW - GUI_TXT_NCR_LABEL_W) / 2, NCR_RY + 18,
                GUI_TXT_NCR_LABEL_W, GUI_TXT_NCR_LABEL_H,
                gui_txt_ncr_label_bits, 1);
      draw_digit_text(NCR_RX + (NCR_RW - dw) / 2, NCR_RY + 62, 3, d,
                      C_BLACK);
    }
}

static void ncr_end(void)
{
  int r = ncr_recognize();

  g_tx = g_ty = -1;
  ncr_show_result(r);

  /* auto-clear the drawing area so the next digit can be written right
   * away; the result panel keeps showing the last recognition */
  fb_fill(HAND_X, HAND_Y, HAND_W, HAND_H, C_WHITE);
  draw_rect(HAND_X, HAND_Y, HAND_W, HAND_H, C_BORDER);
  ncr_reset();
}

static void hand_clear(void)
{
  fb_fill(HAND_X, HAND_Y, HAND_W, HAND_H, C_WHITE);
  draw_rect(HAND_X, HAND_Y, HAND_W, HAND_H, C_BORDER);
  g_ncr_idle = 0;
  ncr_reset();
  ncr_show_result(-1);
}

/* ------------------------------------------------------------------ */
/* page drawing                                                        */
/* ------------------------------------------------------------------ */

static void draw_main(void)
{
  int i;

  fb_fill(0, 0, LCD_W, LCD_H, C_WHITE);

  /* title, upper area, centred */
  blit_center(56, GUI_TITLE_ROW1_W, GUI_TITLE_ROW1_H,
              gui_title_row1_bits, 1);
  blit_center(102, GUI_TITLE_ROW2_W, GUI_TITLE_ROW2_H,
              gui_title_row2_bits, 1);

  /* entry buttons */
  for (i = 0; i < 3; i++)
    {
      const struct btn *b = &g_btns[i];

      draw_rect(b->x, b->y, b->w, b->h, C_BORDER);
      blit_text(b->x + (b->w - b->txt_w) / 2,
                b->y + (b->h - b->txt_h) / 2,
                b->txt_w, b->txt_h, b->txt, 1);
    }
}

static void draw_sub_header(const struct btn *hdr, const uint8_t *hint,
                            int hint_w, int hint_h)
{
  fb_fill(0, 0, LCD_W, LCD_H, C_WHITE);

  /* back button */
  draw_rect(g_back.x, g_back.y, g_back.w, g_back.h, C_BORDER);
  blit_text(g_back.x + (g_back.w - g_back.txt_w) / 2,
            g_back.y + (g_back.h - g_back.txt_h) / 2,
            g_back.txt_w, g_back.txt_h, g_back.txt, 1);

  /* page header centred */
  blit_center(28, hdr->txt_w, hdr->txt_h, hdr->txt, 1);

  if (hint)
    {
      blit_center(66, hint_w, hint_h, hint, 1);
    }
}

static void draw_touch_page(void)
{
  static const struct btn hdr = { 0, 0, 0, 0, PG_TOUCH,
    GUI_TXT_HDR1_W, GUI_TXT_HDR1_H, gui_txt_hdr1_bits };

  draw_sub_header(&hdr, gui_txt_hint_touch_bits,
                  GUI_TXT_HINT_TOUCH_W, GUI_TXT_HINT_TOUCH_H);
}

static void draw_hand_page(void)
{
  static const struct btn hdr = { 0, 0, 0, 0, PG_HAND,
    GUI_TXT_HDR2_W, GUI_TXT_HDR2_H, gui_txt_hdr2_bits };

  draw_sub_header(&hdr, gui_txt_hint_hand_bits,
                  GUI_TXT_HINT_HAND_W, GUI_TXT_HINT_HAND_H);

  /* drawing area */
  draw_rect(HAND_X, HAND_Y, HAND_W, HAND_H, C_BORDER);

  /* clear button */
  draw_rect(CLEAR_X, CLEAR_Y, CLEAR_W, CLEAR_H, C_BORDER);
  blit_text(CLEAR_X + (CLEAR_W - GUI_TXT_CLEAR_W) / 2,
            CLEAR_Y + (CLEAR_H - GUI_TXT_CLEAR_H) / 2,
            GUI_TXT_CLEAR_W, GUI_TXT_CLEAR_H, gui_txt_clear_bits, 1);

  ncr_reset();
  ncr_show_result(-1);
}

/* ------------------------------------------------------------------ */
/* ADC + DAC + FFT spectrum analyser (v55)                             */
/* ------------------------------------------------------------------ */
/* Register-only access (no kernel driver change), same style as the
 * TLI/GT911 bring-up:
 *   ADC0  = 0x40012400, 14-bit, CH18 = PA4 (external signal input)
 *   RCU   = 0x58024400  APB2EN +0x44 bit8=ADC0,
 *                        AHB4EN +0x3C bit0=GPIOA (already on)
 *   GPIOA = 0x58020000  PA4 analog: CTL bits[9:8]=11, PUD bits[9:8]=00
 */
#define VREG(a)     (*(volatile uint32_t *)(a))
#define ADC0_BASE   0x40012400UL
#define RCU_BASE    0x58024400UL
#define GPIOA_BASE  0x58020000UL

#define ADC_STAT    VREG(ADC0_BASE + 0x00)
#define ADC_CTL0    VREG(ADC0_BASE + 0x04)
#define ADC_CTL1    VREG(ADC0_BASE + 0x08)
#define ADC_RSQ0    VREG(ADC0_BASE + 0x24)
#define ADC_RSQ8    VREG(ADC0_BASE + 0x44)
#define ADC_RDATA   VREG(ADC0_BASE + 0x64)
#define ADC_SYNCCTL VREG(ADC0_BASE + 0x304)
#define RCU_APB2EN  VREG(RCU_BASE + 0x44)
#define GPIOA_CTL   VREG(GPIOA_BASE + 0x00)
#define GPIOA_PUD   VREG(GPIOA_BASE + 0x0C)

#define FFT_N   256
#define SP_X    30
#define SP_Y    95
#define SP_W    360
#define SP_H    320
#define FF_X    410
#define FF_Y    95
#define FF_W    360
#define FF_H    320

static float g_re[FFT_N];
static float g_im[FFT_N];
static int   g_spec_inited = 0;
static int   g_adc_min = 0;
static int   g_adc_max = 0;
static int   g_adc_avg = 0;
static int   g_adc_err = 0;


/* RCU APB2 reset control: +0x24, ADC0RST = bit8 (example18 adc_deinit) */
#define RCU_APB2RST  VREG(RCU_BASE + 0x24)
#define RCU_APB4EN   VREG(RCU_BASE + 0x4C)
#define VREF_CS_REG  VREG(0x58003800)

/* enable internal voltage reference: VREFP = 2.5V for ADC */
static void vref_enable(void)
{
  RCU_APB4EN |= (1u << 2);                 /* VREF clock on (APB4EN bit2) */
  VREF_CS_REG = (VREF_CS_REG & ~(0x3u << 4)) | (0u << 4); /* VREFS = 2.5V */
  VREF_CS_REG &= ~(1u << 1);               /* HIPM = 0 (drive VREFP pin) */
  VREF_CS_REG |= (1u << 0);                /* VREFEN = 1 */
  { int w = 0; while (!(VREF_CS_REG & (1u << 3)) && (++w < 100000)) ; }
}

static void adc0_init(void)
{
  vref_enable();
  RCU_APB2EN |= (1u << 8);                 /* ADC0 clock on */
  RCU_APB2RST |= (1u << 8);                /* adc_deinit: reset ADC0 */
  RCU_APB2RST &= ~(1u << 8);

  GPIOA_CTL = (GPIOA_CTL & ~(3u << 8)) | (3u << 8);  /* PA4 analog */
  GPIOA_PUD &= ~(3u << 8);

  ADC_CTL1 &= ~(1u << 0);                  /* ADC off during config */
  ADC_SYNCCTL = (ADC_SYNCCTL & ~((0xFu << 16) | (0xFu << 20))) | 0x000C0000u;
  ADC_SYNCCTL &= ~(0xFu << 0);             /* sync mode: independent (=0) */
  ADC_CTL0 &= ~(0x3u << 24);               /* 14-bit resolution */
  ADC_CTL0 &= ~(1u << 8);                  /* scan mode off */
  ADC_CTL1 &= ~(1u << 1);                  /* continuous mode off */
  ADC_CTL1 &= ~(1u << 11);                 /* right aligned */
  ADC_CTL1 &= ~(0x3u << 28);               /* ext trigger disable (ETMRC=0) */
  ADC_RSQ0 &= ~(0xFu << 20);               /* 1 conversion in sequence */
  ADC_RSQ8 = ((807u & 0x3FFu) << 5) | 0x12u;  /* rank0 = CH18, 807-clk */

  ADC_CTL1 |= (1u << 0);                   /* ADC on */
  usleep(10000);                           /* wait ADC stable, then calibrate */
  ADC_CTL1 |= (1u << 27);                  /* calibration mode OFFSET (lib) */
  ADC_CTL1 &= ~(0x7u << 4);                /* calibration number: 1 */
  ADC_CTL1 |= (1u << 3);                   /* reset calibration */
  while (ADC_CTL1 & (1u << 3));
  ADC_CTL1 |= (1u << 2);                   /* start calibration */
  while (ADC_CTL1 & (1u << 2));

  printf("SPECTRUM: ADC0 CH18(PA4) 14bit ready\n");
}

static void spectrum_init_once(void)
{
  if (!g_spec_inited)
    {
      adc0_init();
      g_spec_inited = 1;
    }
}

/* Sample FFT_N points (software trigger) from external signal on PA4. */
static void adc_sample(void)
{
  int i;

  RCU_APB2EN |= (1u << 8);             /* keep ADC0 clock alive (some path clears it) */

  for (i = 0; i < FFT_N; i++)
    {
      ADC_RSQ8 = ((807u & 0x3FFu) << 5) | 0x12u;  /* re-arm rank0 = CH18 */
      ADC_STAT = ~(1u << 1);               /* clear EOC (stdlib adc_flag_clear) */
      ADC_CTL1 |= (1u << 30);              /* software start */
      {
        int spins = 0;

        while (!(ADC_STAT & (1u << 1)))    /* wait EOC, bounded */
          {
            if (++spins > 200000)
              {
                g_adc_err++;
                break;
              }
          }
      }
      g_re[i] = (float)(ADC_RDATA & 0x3FFF);
      g_im[i] = 0.0f;

      {
        int v = (int)g_re[i];

        if (i == 0) { g_adc_min = g_adc_max = v; }
        if (v < g_adc_min) { g_adc_min = v; }
        if (v > g_adc_max) { g_adc_max = v; }
        g_adc_avg += v;
      }
    }

  g_adc_avg /= FFT_N;
}

/* In-place radix-2 FFT, float.  Twiddle constants are precomputed per
 * stage (no math.h dependency). */
static void fft_radix2(void)
{
  static const float tw[8][2] =
  {
    { -1.0f, 0.0f },           /* len=2   */
    {  0.0f, 1.0f },           /* len=4   */
    {  0.70710678f,  0.70710678f },
    {  0.92387953f,  0.38268343f },
    {  0.98078528f,  0.19509032f },
    {  0.99518473f,  0.09801714f },
    {  0.99879546f,  0.04906767f },
    {  0.99969882f,  0.02454123f },
  };
  int n = FFT_N, i, j, len, k;

  for (i = 1, j = 0; i < n; i++)
    {
      int bit = n >> 1;

      for (; j & bit; bit >>= 1) { j ^= bit; }
      j ^= bit;
      if (i < j)
        {
          float tr = g_re[i]; g_re[i] = g_re[j]; g_re[j] = tr;
          float ti = g_im[i]; g_im[i] = g_im[j]; g_im[j] = ti;
        }
    }

  for (len = 2, k = 0; len <= n; len <<= 1, k++)
    {
      float wr = tw[k][0], wi = tw[k][1];

      for (i = 0; i < n; i += len)
        {
          float cwr = 1.0f, cwi = 0.0f;
          int half = len >> 1;

          for (j = 0; j < half; j++)
            {
              int a = i + j;
              int b = a + half;
              float tr = g_re[b] * cwr - g_im[b] * cwi;
              float ti = g_re[b] * cwi + g_im[b] * cwr;

              g_re[b] = g_re[a] - tr;
              g_im[b] = g_im[a] - ti;
              g_re[a] += tr;
              g_im[a] += ti;

              {
                float nwr = cwr * wr - cwi * wi;
                cwi = cwr * wi + cwi * wr;
                cwr = nwr;
              }
            }
        }
    }
}

static void spectrum_draw(void)
{
  int i, peak = 1;
  float pmax = 0.0f;

  spectrum_init_once();
  adc_sample();                       /* fill g_re[] with real samples */

  /* left: time-domain waveform */
  fb_fill(SP_X, SP_Y, SP_W, SP_H, C_WHITE);
  for (i = 1; i < FFT_N; i++)
    {
      int x0 = SP_X + (i - 1) * SP_W / FFT_N;
      int y0 = SP_Y + (int)((16383.0f - g_re[i - 1]) * SP_H / 16384.0f);
      int x1 = SP_X + i * SP_W / FFT_N;
      int y1 = SP_Y + (int)((16383.0f - g_re[i]) * SP_H / 16384.0f);

      draw_line(x0, y0, x1, y1, 2, C_BLACK);
    }
  draw_rect(SP_X, SP_Y, SP_W, SP_H, C_BORDER);

  /* right: magnitude spectrum, 128 bins, fixed 14-bit full-scale */
  for (i = 1; i < FFT_N / 2; i++)
    {
      float mag = sqrtf(g_re[i] * g_re[i] + g_im[i] * g_im[i]);

      if (mag > pmax) { pmax = mag; peak = i; }
    }

  fb_fill(FF_X, FF_Y, FF_W, FF_H, C_WHITE);
  for (i = 1; i < FFT_N / 2; i++)
    {
      float mag = sqrtf(g_re[i] * g_re[i] + g_im[i] * g_im[i]);
      int h = (int)(mag * SP_H / 1040.0f);

      if (h > SP_H) { h = SP_H; }
      if (h > 0)
        {
          fb_fill(FF_X + (FF_W - 256) / 2 + i * 2,
                  FF_Y + SP_H - h, 2, h,
                  (i == peak) ? C_BLACK : C_HILITE);
        }
    }
  draw_rect(FF_X, FF_Y, FF_W, FF_H, C_BORDER);

  /* peak frequency (Hz) in the top-right corner of the spectrum panel */
  {
    char buf[16];
    int w;

    snprintf(buf, sizeof(buf), "%d",
             (int)(36363.0f * peak / FFT_N));
    w = digit_text_w(2, buf);
    draw_digit_text(FF_X + FF_W - w - 10, FF_Y + 8, 2, buf, C_BLACK);
  }
}

static void draw_meas_page(void)
{
  static const struct btn hdr = { 0, 0, 0, 0, PG_MEAS,
    GUI_TXT_HDR3_W, GUI_TXT_HDR3_H, gui_txt_hdr3_bits };

  draw_sub_header(&hdr, NULL, 0, 0);
  spectrum_draw();
}

static void draw_sys_page(void)
{
  static const struct btn hdr = { 0, 0, 0, 0, PG_SYS,
    GUI_TXT_HDR4_W, GUI_TXT_HDR4_H, gui_txt_hdr4_bits };
  static const struct { const uint8_t *b; int w, h; } rows[6] =
  {
    { gui_txt_s_line1_bits, GUI_TXT_S_LINE1_W, GUI_TXT_S_LINE1_H },
    { gui_txt_s_line2_bits, GUI_TXT_S_LINE2_W, GUI_TXT_S_LINE2_H },
    { gui_txt_s_line3_bits, GUI_TXT_S_LINE3_W, GUI_TXT_S_LINE3_H },
    { gui_txt_s_line4_bits, GUI_TXT_S_LINE4_W, GUI_TXT_S_LINE4_H },
    { gui_txt_s_line5_bits, GUI_TXT_S_LINE5_W, GUI_TXT_S_LINE5_H },
    { gui_txt_s_line6_bits, GUI_TXT_S_LINE6_W, GUI_TXT_S_LINE6_H },
  };
  int i;

  draw_sub_header(&hdr, NULL, 0, 0);
  for (i = 0; i < 6; i++)
    {
      blit_center(170 + i * 42, rows[i].w, rows[i].h, rows[i].b, 1);
    }
}

static void draw_page(void)
{
  if (g_cal9 < C9_PTS)
    {
      /* Touch calibration pages.  g_cal9 is set to C9_PTS at startup, so
       * this never triggers; the code is kept for completeness.
       */
      return;
    }

  if (g_cal != CAL_NONE)
    {
      return;
    }

  switch (g_page)
    {
    case PG_MAIN:  draw_main();        break;
    case PG_TOUCH: draw_touch_page();  break;
    case PG_HAND:  draw_hand_page();   break;
    case PG_MEAS:  draw_meas_page();   break;
    case PG_SYS:   draw_sys_page();    break;
    }
}

/* ------------------------------------------------------------------ */
/* touch handling                                                      */
/* ------------------------------------------------------------------ */

static void touch_main(int x, int y)
{
  int i;

  for (i = 0; i < 3; i++)
    {
      const struct btn *b = &g_btns[i];

      if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h)
        {
          /* pressed feedback */
          fb_fill(b->x, b->y, b->w, b->h, C_HILITE);
          blit_text(b->x + (b->w - b->txt_w) / 2,
                    b->y + (b->h - b->txt_h) / 2,
                    b->txt_w, b->txt_h, b->txt, 1);
          usleep(60000);
          g_page = b->page;
          g_tx = g_ty = -1;
          ncr_reset();
          draw_page();
          return;
        }
    }
}

static void touch_sub(int x, int y)
{
  if (x >= g_back.x && x < g_back.x + g_back.w &&
      y >= g_back.y && y < g_back.y + g_back.h)
    {
      fb_fill(g_back.x, g_back.y, g_back.w, g_back.h, C_HILITE);
      blit_text(g_back.x + (g_back.w - g_back.txt_w) / 2,
                g_back.y + (g_back.h - g_back.txt_h) / 2,
                g_back.txt_w, g_back.txt_h, g_back.txt, 1);
      usleep(60000);
      g_page = PG_MAIN;
      g_tx = g_ty = -1;
      ncr_reset();
      draw_page();
    }
}

static void touch_show_coord(int x, int y)
{
  char buf[24];
  int w;

  snprintf(buf, sizeof(buf), "x:%d, y:%d", x, y);
  w = digit_text_w(1, buf);

  /* top-right corner, right-aligned */
  fb_fill(LCD_W - 260, 12, 250, GUI_DIG_H + 8, C_WHITE);
  draw_digit_text(LCD_W - 24 - w, 16, 1, buf, C_BLACK);
}

static void touch_update(int x, int y)
{
  switch (g_page)
    {
    case PG_MAIN:
      touch_main(x, y);
      break;

    case PG_TOUCH:
      touch_sub(x, y);
      if (g_page != PG_TOUCH) { break; }
      if (g_tx >= 0 && g_ty >= 0)
        {
          draw_line(g_tx, g_ty, x, y, 6, C_BLACK);
        }
      else
        {
          draw_circle(x, y, 3, C_BLACK);
        }
      touch_show_coord(x, y);
      g_tx = x;
      g_ty = y;
      break;

    case PG_HAND:
      touch_sub(x, y);
      if (g_page != PG_HAND) { break; }

      if (x >= CLEAR_X && x < CLEAR_X + CLEAR_W &&
          y >= CLEAR_Y && y < CLEAR_Y + CLEAR_H)
        {
          hand_clear();
          break;
        }

      if (x >= HAND_X && x < HAND_X + HAND_W &&
          y >= HAND_Y && y < HAND_Y + HAND_H)
        {
          g_ncr_idle = 0;   /* touching again: cancel the pending result */
          if (g_tx >= 0 && g_ty >= 0)
            {
              draw_line(g_tx, g_ty, x, y, 6, C_BLACK);
            }
          else
            {
              draw_circle(x, y, 3, C_BLACK);
            }
          ncr_add(x, y);
          g_tx = x;
          g_ty = y;
        }
      break;

    case PG_MEAS:
    case PG_SYS:
      touch_sub(x, y);
      break;
    }
}

/* ------------------------------------------------------------------ */
/* console fallback (1-4 enter pages, b back)                          */
/* ------------------------------------------------------------------ */

static int gui_anykey(void)
{
  struct timeval tv = { 0, 0 };
  fd_set fds;
  int ret;

  FD_ZERO(&fds);
  FD_SET(0, &fds);
  ret = select(1, &fds, NULL, NULL, &tv);
  return (ret > 0);
}

static void console_key(void)
{
  char c;

  if (read(0, &c, 1) != 1)
    {
      return;
    }

  if (c == '1')
    {
      g_page = PG_TOUCH;
      g_tx = g_ty = -1;
      ncr_reset();
      draw_page();
    }
  else if (c == '2')
    {
      g_page = PG_MEAS;
      g_tx = g_ty = -1;
      ncr_reset();
      draw_page();
    }
  else if (c == '3')
    {
      g_page = PG_SYS;
      g_tx = g_ty = -1;
      ncr_reset();
      draw_page();
    }
  else if (c == 'b' || c == 'B')
    {
      g_page = PG_MAIN;
      g_tx = g_ty = -1;
      ncr_reset();
      draw_page();
    }
  else if (c == 'q' || c == 'Q')
    {
      g_run = false;
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, FAR char *argv[])
{
  int ret;

  if (board_lcd_enable() != OK)
    {
      fprintf(stderr, "GUI: LCD enable failed\n");
      return EXIT_FAILURE;
    }

  ret = gt911_lower_init();
  if (ret != OK)
    {
      fprintf(stderr, "GUI: touch init failed %d (keyboard only)\n", ret);
    }

  g_cal = CAL_NONE;
  g_cal9 = C9_PTS;   /* raw GT911 coords already map 1:1 onto the 800x480
                      * panel, so skip the touch calibration pages */
  g_lrx = g_lry = -1;
  g_page = PG_MAIN;
  ncr_reset();
  draw_page();
  printf("GUI: ready, touch buttons (q to quit)\n");

  while (g_run)
    {
      int x = -1;
      int y = -1;
      int down = 0;

      if (ret == OK)
        {
          if (gt911_lower_scan(&x, &y, &down) == OK)
            {
              if (down)
                {
                  touch_update(x, y);
                }
              else if (g_page == PG_HAND && g_ncr_n > 0 &&
                       g_ncr_idle == 0)
                {
                  /* finger lifted (edge only): start idle counting,
                   * recognise only after NCR_IDLE_LOOPS of no new touch
                   * (example 33).  The g_ncr_idle==0 guard makes this a
                   * one-shot edge trigger, not a per-loop reset. */
                  g_ncr_idle = 1;
                  g_tx = g_ty = -1;
                }
              else if (g_page == PG_TOUCH || g_page == PG_HAND)
                {
                  g_tx = g_ty = -1;  /* end stroke */
                }
            }
        }

      if (g_page == PG_HAND && g_ncr_idle > 0)
        {
          if (++g_ncr_idle > NCR_IDLE_LOOPS)
            {
              g_ncr_idle = 0;
              ncr_end();   /* stroke finished: recognise + auto-clear */
            }
        }

      if (gui_anykey())
        {
          console_key();
        }

      if (g_page == PG_MEAS)
        {
          adc_sample();
          fft_radix2();
          spectrum_draw();
        }

      usleep(20000);
    }

  printf("GUI: stopped, back to shell\n");
  return OK;
}
