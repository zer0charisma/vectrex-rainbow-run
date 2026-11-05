/*
 * font.h - bubble-letter font for the celebration text (generated).
 * Same style as the title letters. Same format as shapes.h: number of
 * outlines, then per outline: move dy, dx, Draw_VLc list (count-1, pairs).
 * Coordinates are doubled (like the title) and each glyph starts at its
 * own left edge; glyph_adv() gives how far to move before the next one.
 */
#ifndef FONT_H
#define FONT_H
#include <vectrex/bios.h>

static const int8_t glyph_R[] = { 2, 0, 0, 10, 62, 0, 10, 10, 0, 26, -12, 12, -16, 0, -10, -12, -34, 12, 0, -18, 28, -10, -28, -4, 0, -16, 44, 16, 3, 14, 0, -4, 18, -10, -4, 0, -14 };
static const int8_t glyph_A[] = { 2, 0, 0, 8, 52, 0, 20, 28, -20, 20, -52, 0, 0, -16, 20, 0, 0, -16, -20, 0, 0, -16, 34, 16, 3, 14, 0, 8, 10, -22, 6, 0, -16 };
static const int8_t glyph_I[] = { 1, 4, 0, 3, 0, 24, 68, -4, -4, -20, -64, 0 };
static const int8_t glyph_N[] = { 1, 4, 0, 9, -4, 16, 40, 0, -40, 16, 4, 16, 68, -4, 0, -12, -40, 0, 40, -16, -4, -16, -64, 0 };
static const int8_t glyph_B[] = { 3, 0, 0, 7, 72, 0, 0, 34, -10, 12, -24, -6, -8, 8, -20, 0, -10, -10, 0, -38, 44, 16, 3, 14, 0, -4, 16, -10, -4, 0, -12, -30, 0, 3, 14, 0, -4, 18, -10, -4, 0, -14 };
static const int8_t glyph_O[] = { 2, 16, 0, 7, 40, 0, 16, 16, 0, 16, -16, 16, -40, 0, -16, -16, 0, -16, 16, -16, 6, 16, 4, 28, 0, 6, 12, -34, 4, -6, -4, 6, -12 };
static const int8_t glyph_W[] = { 1, 68, 0, 11, 4, 16, -44, 2, 16, 6, -16, 6, 44, 2, -4, 16, -68, -10, 0, -8, 16, -6, -16, -6, 4, -12, 64, -6 };
static const int8_t glyph_U[] = { 1, 68, 0, 9, 4, 16, -56, 4, 4, 12, 52, 0, -4, 16, -52, 0, -16, -16, 0, -16, 16, -16, 52, 0 };
static const int8_t glyph_L[] = { 1, 68, 0, 5, 4, 16, -56, 0, 0, 28, -16, 0, 4, -44, 64, 0 };
static const int8_t glyph_E[] = { 1, 4, 0, 11, 64, 0, 4, 44, -16, 0, 0, -28, -12, 0, -4, 24, -12, -4, 0, -20, -12, 0, 0, 28, -12, 4, 0, -48 };
static const int8_t glyph_V[] = { 1, 68, 0, 6, 4, 16, -44, 8, 44, 8, -4, 16, -68, -22, 2, -8, 66, -18 };
static const int8_t glyph_C[] = { 1, 16, 0, 9, 40, 0, 16, 16, 0, 28, -16, 0, -4, -28, -32, 0, -4, 28, -12, 4, -4, -32, 16, -16 };
static const int8_t glyph_M[] = { 1, 4, 0, 11, 64, 0, 4, 14, -20, 10, 20, 10, 0, 10, -68, 4, -4, -12, 40, 0, -20, -12, 20, -12, -40, 0, 4, -12 };
static const int8_t glyph_P[] = { 2, 4, 0, 7, 64, 0, 4, 36, -12, 12, -20, 0, -12, -12, 0, -20, -28, -4, 4, -12, 38, 16, 3, 16, 0, -4, 18, -12, -4, 0, -14 };
static const int8_t glyph_T[] = { 1, 68, 0, 7, 0, 48, -12, -4, 0, -12, -56, -4, 4, -12, 52, 0, 0, -12, 12, -4 };
static const int8_t glyph_H[] = { 1, 4, 0, 11, 64, 0, 4, 12, -28, 4, 0, 16, 24, 0, 4, 12, -68, 4, 0, -16, 24, 0, 0, -16, -28, -4, 4, -12 };
static const int8_t glyph_Y[] = { 1, 68, 0, 8, 4, 16, -20, 8, 20, 8, -4, 16, -32, -16, -36, -4, 4, -12, 32, 0, 32, -16 };
static const int8_t glyph_D[] = { 2, 0, 0, 5, 72, 0, 0, 32, -16, 16, -40, 0, -16, -16, 0, -32, 16, 16, 3, 40, 0, 0, 10, -34, 6, -6, -16 };
static const int8_t glyph_K[] = { 1, 4, 0, 10, 64, 0, 0, 16, -24, 0, 28, 16, 0, 12, -36, -14, -32, 18, -4, -16, 28, -16, -24, 0, 0, -16 };
static const int8_t glyph_S[] = { 1, 12, 0, 15, -12, 12, 0, 24, 12, 12, 20, 0, 12, -12, 2, -20, 10, 2, 0, 26, 16, 0, 0, -32, -12, -12, -20, 0, -12, 12, -2, 20, -10, -2, -4, -30 };
static const int8_t glyph_G[] = { 1, 16, 0, 12, 40, 0, 16, 16, 0, 28, -16, 0, -4, -28, -32, 0, -4, 16, 22, -6, 0, 18, -26, 4, -12, -12, 0, -20, 16, -16 };
static const int8_t glyph_EXCL[] = { 2, 68, 0, 3, 0, 16, -48, -6, 4, -8, 44, -2, -64, 2, 3, -4, 8, 8, 4, 4, -8, -8, -4 };
static const int8_t glyph_APOS[] = { 1, 72, 0, 2, 0, 8, -20, -6, 20, -2 };
static const int8_t glyph_1[] = { 1, 54, 0, 6, 18, 16, -4, 14, -68, -4, 4, -12, 44, 0, -6, -14, 12, 0 };
static const int8_t glyph_2[] = { 1, 56, 0, 11, 16, 16, 0, 20, -12, 12, -16, 0, -28, -26, 0, 22, -16, 0, 4, -44, 12, 0, 28, 30, 12, -4, 0, -26 };
static const int8_t glyph_3[] = { 1, 60, 0, 14, 12, 12, 0, 24, -12, 12, -24, -8, -24, 8, -12, -12, 0, -24, 12, -12, 8, 4, 0, 28, 10, -12, 12, 0, 10, 12, 0, -28, 8, -4 };
static const int8_t glyph_4[] = { 2, 72, 24, 6, -4, 20, -48, 4, -20, -8, 4, -12, 12, 0, 4, -28, 52, 24, -40, 4, 2, 20, 0, -20, -12, 0, 12 };
static const int8_t glyph_5[] = { 1, 68, 0, 14, 0, 48, -12, -4, 0, -28, -8, 0, 0, 20, -12, 12, -24, 0, -12, -12, 0, -24, 12, -12, 8, 4, 0, 28, 12, -4, 4, -28, 32, 0 };

/* The glyph for a character (0 for a space) and its advance width */
static const int8_t *glyph(char c)
{
    switch (c) {
    case 'R': return glyph_R;
    case 'A': return glyph_A;
    case 'I': return glyph_I;
    case 'N': return glyph_N;
    case 'B': return glyph_B;
    case 'O': return glyph_O;
    case 'W': return glyph_W;
    case 'U': return glyph_U;
    case 'L': return glyph_L;
    case 'E': return glyph_E;
    case 'V': return glyph_V;
    case 'C': return glyph_C;
    case 'M': return glyph_M;
    case 'P': return glyph_P;
    case 'T': return glyph_T;
    case 'H': return glyph_H;
    case 'Y': return glyph_Y;
    case 'D': return glyph_D;
    case 'K': return glyph_K;
    case 'S': return glyph_S;
    case 'G': return glyph_G;
    case '!': return glyph_EXCL;
    case '\'': return glyph_APOS;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case '5': return glyph_5;
    default:  return 0;
    }
}

static uint8_t glyph_adv(char c)
{
    switch (c) {
    case 'I': return 18;
    case 'L': return 28;
    case '!': return 14;
    case '\'': return 10;
    case '1': return 21;
    case ' ': return 14;
    default:  return 30;
    }
}

#endif
