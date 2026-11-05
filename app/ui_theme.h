/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
/* All operational screen colors live here. RGB565 constants stay in flash;
 * track order is BL_VID_ (Heartbeat before Drone), not Composer role order.
 * Source red and timing grey remain invariant, including in MONO. All base
 * surfaces are exactly zero in RGB565. Accents use the requested RGB888
 * values; RGB() truncates to 5/6/5 bits, without mixing them toward grey. */
typedef struct {
    uint16_t background, panel, strip, step, line;
    uint16_t text, secondary, dim, disabled;
    uint16_t focus, info, locked, muted, source, timing, suppressed, inverse;
    uint16_t track[BL_NVIDS];
} ui_palette_t;

static const ui_palette_t UI_PALETTE[DEVICE_NTHEMES] = {
    [DEVICE_THEME_CLASSIC] = {
        .background = RGB(0,0,0), .panel = RGB(0,0,0), .strip = RGB(0,0,0),
        .step = RGB(0,0,0), .line = RGB(44,44,44), .text = RGB(231,236,242),
        .secondary = RGB(170,178,189), .dim = RGB(126,134,146), .disabled = RGB(96,105,115),
        .focus = RGB(255,158,74), .info = RGB(79,217,242), .locked = RGB(246,193,107),
        .muted = RGB(255,106,79), .source = RGB(255,32,32), .timing = RGB(126,126,126),
        .suppressed = RGB(96,105,115), .inverse = RGB(0,0,0),
        .track = {RGB(95,184,201), RGB(95,158,201), RGB(201,151,95), RGB(155,127,201),
                  RGB(217,221,227), RGB(201,111,111), RGB(95,111,168)}
    },
    [DEVICE_THEME_GRAPHITE] = {
        .background = RGB(0,0,0), .panel = RGB(0,0,0), .strip = RGB(0,0,0),
        .step = RGB(0,0,0), .line = RGB(40,40,40), .text = RGB(213,229,234),
        .secondary = RGB(95,201,216), .dim = RGB(176,176,176), .disabled = RGB(136,136,136),
        .focus = RGB(95,201,216), .info = RGB(35,155,168), .locked = RGB(213,229,234),
        .muted = RGB(160,160,160), .source = RGB(255,32,32), .timing = RGB(126,126,126),
        .suppressed = RGB(104,104,104), .inverse = RGB(0,0,0),
        .track = {RGB(95,201,216), RGB(35,155,168), RGB(95,201,216), RGB(35,155,168),
                  RGB(213,229,234), RGB(35,155,168), RGB(95,201,216)}
    },
    [DEVICE_THEME_AMBER] = {
        .background = RGB(0,0,0), .panel = RGB(0,0,0), .strip = RGB(0,0,0),
        .step = RGB(0,0,0), .line = RGB(52,52,52), .text = RGB(245,213,173),
        .secondary = RGB(255,180,90), .dim = RGB(150,150,150), .disabled = RGB(100,100,100),
        .focus = RGB(214,92,24), .info = RGB(255,180,90), .locked = RGB(245,213,173),
        .muted = RGB(150,150,150), .source = RGB(255,32,32), .timing = RGB(126,126,126),
        .suppressed = RGB(112,112,112), .inverse = RGB(0,0,0),
        .track = {RGB(214,92,24), RGB(255,180,90), RGB(214,92,24), RGB(255,180,90),
                  RGB(245,213,173), RGB(214,92,24), RGB(255,180,90)}
    },
    [DEVICE_THEME_NOCTURNE] = {
        .background = RGB(0,0,0), .panel = RGB(0,0,0), .strip = RGB(0,0,0),
        .step = RGB(0,0,0), .line = RGB(52,52,52), .text = RGB(230,217,255),
        .secondary = RGB(238,87,188), .dim = RGB(150,150,150), .disabled = RGB(100,100,100),
        .focus = RGB(164,106,255), .info = RGB(238,87,188), .locked = RGB(230,217,255),
        .muted = RGB(150,150,150), .source = RGB(255,32,32), .timing = RGB(126,126,126),
        .suppressed = RGB(112,112,112), .inverse = RGB(0,0,0),
        .track = {RGB(164,106,255), RGB(238,87,188), RGB(164,106,255), RGB(238,87,188),
                  RGB(230,217,255), RGB(238,87,188), RGB(164,106,255)}
    },
    [DEVICE_THEME_MONO] = {
        .background = RGB(0,0,0), .panel = RGB(0,0,0), .strip = RGB(0,0,0),
        .step = RGB(0,0,0), .line = RGB(70,70,70), .text = RGB(208,208,208),
        .secondary = RGB(136,136,136), .dim = RGB(136,136,136), .disabled = RGB(112,112,112),
        .focus = RGB(245,245,245), .info = RGB(136,136,136), .locked = RGB(220,220,220),
        .muted = RGB(185,185,185), .source = RGB(255,32,32), .timing = RGB(126,126,126),
        .suppressed = RGB(128,128,128), .inverse = RGB(0,0,0),
        .track = {RGB(192,192,192), RGB(168,168,168), RGB(208,208,208), RGB(176,176,176),
                  RGB(224,224,224), RGB(160,160,160), RGB(184,184,184)}
    },
    [DEVICE_THEME_FIRE] = {
        .background = RGB(0,0,0), .panel = RGB(0,0,0), .strip = RGB(0,0,0),
        .step = RGB(0,0,0), .line = RGB(52,52,52), .text = RGB(255,224,210),
        .secondary = RGB(212,124,50), .dim = RGB(150,150,150), .disabled = RGB(100,100,100),
        .focus = RGB(255,16,24), .info = RGB(212,124,50), .locked = RGB(255,224,210),
        .muted = RGB(150,150,150), .source = RGB(255,32,32), .timing = RGB(126,126,126),
        .suppressed = RGB(112,112,112), .inverse = RGB(0,0,0),
        .track = {RGB(255,16,24), RGB(212,124,50), RGB(255,16,24), RGB(255,16,24),
                  RGB(255,224,210), RGB(212,124,50), RGB(255,16,24)}
    }
};

static const ui_palette_t *ui_colors(void)
{
    unsigned theme = device_settings_get().theme;
    return &UI_PALETTE[theme < DEVICE_NTHEMES ? theme : DEVICE_THEME_CLASSIC];
}
