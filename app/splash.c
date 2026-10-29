/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Startup only. Approved Tangram logo geometry supplied with TGM-005B:
 * original 240x240 coordinates, uniformly rasterized to exactly 72x72 pixels.
 * Flat representative colors replace the original subtle gradients. */

enum { TGM_SPLASH_SOURCE = 240, TGM_SPLASH_LOGO = 72 };
#define TGM_SPLASH_BG RGB(7, 19, 23)
#define TGM_SPLASH_OUTLINE RGB(3, 16, 19)

typedef struct {
    uint8_t n;
    uint16_t color;
    uint8_t xy[4][2];
} tgm_splash_piece_t;

static const tgm_splash_piece_t TGM_SPLASH_PIECE[7] = {
    {3, RGB(35, 155, 168), {{24, 34}, {114, 34}, {74, 84}}},
    {3, RGB(230, 149, 48), {{120, 34}, {218, 34}, {169, 84}}},
    {4, RGB(244, 203, 131), {{118, 48}, {151, 81}, {118, 114}, {85, 81}}},
    {3, RGB(35, 155, 168), {{87, 117}, {158, 159}, {87, 203}}},
    {3, RGB(20, 125, 137), {{87, 117}, {158, 117}, {158, 159}}},
    {3, RGB(230, 149, 48), {{158, 161}, {158, 210}, {125, 178}}},
    {4, RGB(20, 125, 137), {{87, 210}, {124, 210}, {158, 224}, {87, 224}}},
};

/* These seven supplied polygons are convex and consistently wound. Evaluate
 * half-planes at output-pixel centers in source space, avoiding independent
 * vertex rounding or aspect-ratio changes. The outline is centered on each
 * edge with the approved four-source-pixel width (two pixels on either side).
 * Integer edge lengths introduce only subpixel raster rounding. */
static void tgm_splash_piece(const tgm_splash_piece_t *p)
{
    struct { int32_t x, y, dx, dy, border; } edge[4];
    int min_x = TGM_SPLASH_SOURCE, min_y = TGM_SPLASH_SOURCE, max_x = 0, max_y = 0;
    const int den = 2 * TGM_SPLASH_LOGO;
    for (int i = 0; i < p->n; i++) {
        int j = (i + 1) % p->n;
        int x = p->xy[i][0], y = p->xy[i][1];
        int dx = p->xy[j][0] - x, dy = p->xy[j][1] - y;
        int length = 0, squared = dx * dx + dy * dy;
        while ((length + 1) * (length + 1) <= squared) length++;
        edge[i].x = x * den;
        edge[i].y = y * den;
        edge[i].dx = dx;
        edge[i].dy = dy;
        edge[i].border = 2 * den * length;
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
    }
    /* All approved vertices leave a margin greater than the source outline.
     * Pad the small raster bounds for pointed outline joins, then clip safely. */
    int x0 = min_x * TGM_SPLASH_LOGO / TGM_SPLASH_SOURCE - 4;
    int y0 = min_y * TGM_SPLASH_LOGO / TGM_SPLASH_SOURCE - 4;
    int x1 = max_x * TGM_SPLASH_LOGO / TGM_SPLASH_SOURCE + 4;
    int y1 = max_y * TGM_SPLASH_LOGO / TGM_SPLASH_SOURCE + 4;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= TGM_SPLASH_LOGO) x1 = TGM_SPLASH_LOGO - 1;
    if (y1 >= TGM_SPLASH_LOGO) y1 = TGM_SPLASH_LOGO - 1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            int inside = 1, outline = 0;
            int px = (2 * x + 1) * TGM_SPLASH_SOURCE;
            int py = (2 * y + 1) * TGM_SPLASH_SOURCE;
            for (int i = 0; i < p->n; i++) {
                int32_t cross = edge[i].dx * (py - edge[i].y) - edge[i].dy * (px - edge[i].x);
                if (cross < -edge[i].border) { inside = 0; break; }
                if (cross <= edge[i].border) outline = 1;
            }
            if (inside) cv_pset(x, y, outline ? TGM_SPLASH_OUTLINE : p->color);
        }
    }
}

void app_splash(void)
{
    lcd_fill(0, 0, 240, 240, TGM_SPLASH_BG);
    cv_begin(TGM_SPLASH_LOGO, TGM_SPLASH_LOGO, TGM_SPLASH_BG);
    for (int i = 0; i < 7; i++) tgm_splash_piece(&TGM_SPLASH_PIECE[i]);
    cv_blit((240 - TGM_SPLASH_LOGO) / 2, 52);

    /* Reuse the current font assets and canvas. No sleeps, animation or changes
     * to boot sequencing; the platform's splash() retains its final lcd_sync. */
    const felucca_font_t *font = &APP_BIG_FONT;
    if (font->h > 48 || text_w(font, "Tangram") > 216) font = &FONT_M;
    if (font->h > 48 || text_w(font, "Tangram") > 216) font = &FONT_B;
    cv_begin(240, 48, TGM_SPLASH_BG);
    cv_text((240 - text_w(font, "Tangram")) / 2, (48 - font->h) / 2,
            font, "Tangram", RGB(231, 236, 242));
    cv_blit(0, 140);
}
