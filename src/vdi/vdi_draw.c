#include <windows.h>
#include "vdi/vdi_draw.h"
#include "vdi/vdi_attr.h"
#include "screen_canvas.h"
#include "m68k.h"
#include "logger.h"

// CLIPPING

// VDI rectangles are two inclusive corners, not GEM's x,y,w,h; hence the "+1"
// conversions throughout this file.

// [FULL] Clipping resolves against the AES's window list, so a clip matching no window still narrows drawing rather than being ignored.
void vdi_vs_clip(VdiPB *pb) {
    int16_t flag = vdi_pb_intin(pb, 0);
    if (flag) {
        int16_t x1 = vdi_pb_ptsin(pb, 0);
        int16_t y1 = vdi_pb_ptsin(pb, 1);
        int16_t x2 = vdi_pb_ptsin(pb, 2);
        int16_t y2 = vdi_pb_ptsin(pb, 3);
        int matched = canvas_set_clip(x1, y1, (int16_t)(x2 - x1 + 1), (int16_t)(y2 - y1 + 1));
        log_write(LOG_API, "vs_clip(on, %d,%d-%d,%d) -> %s", x1, y1, x2, y2, matched ? "matched a window" : "no matching window");
    } else {
        canvas_clear_clip();
        log_write(LOG_API, "vs_clip(off)");
    }
}

// FILLED RECTANGLES

// v_bar and vr_recfl differ only in their binding, so both share this.
static void fill_rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, const char *name) {
    int16_t w = (int16_t)(x2 - x1 + 1);
    int16_t h = (int16_t)(y2 - y1 + 1);

    if (vdi_attr_write_mode_is_xor()) {
        canvas_invert(x1, y1, w, h);
        log_write(LOG_API, "%s(%d,%d)-(%d,%d) -> XOR, inverted", name, x1, y1, x2, y2);
        return;
    }

    COLORREF color = vdi_attr_fill_rgb();
    canvas_fill(x1, y1, w, h, (unsigned int)color);
    log_write(LOG_API, "%s(%d,%d)-(%d,%d) -> RGB(%lu,%lu,%lu)", name, x1, y1, x2, y2, (unsigned long)GetRValue(color), (unsigned long)GetGValue(color), (unsigned long)GetBValue(color));
}

static void vdi_v_bar(VdiPB *pb) {
    int16_t x1 = vdi_pb_ptsin(pb, 0);
    int16_t y1 = vdi_pb_ptsin(pb, 1);
    int16_t x2 = vdi_pb_ptsin(pb, 2);
    int16_t y2 = vdi_pb_ptsin(pb, 3);
    fill_rect(x1, y1, x2, y2, "v_bar");
}

const char *vdi_gdp_name(int16_t sub) {
    switch (sub) {
        case GDP_BAR:       return "v_bar";
        case GDP_ARC:       return "v_arc";
        case GDP_PIESLICE:  return "v_pieslice";
        case GDP_CIRCLE:    return "v_circle";
        case GDP_ELLIPSE:   return "v_ellipse";
        case GDP_ELLARC:    return "v_ellarc";
        case GDP_ELLPIE:    return "v_ellpie";
        case GDP_RBOX:      return "v_rbox";
        case GDP_RFBOX:     return "v_rfbox";
        case GDP_JUSTIFIED: return "v_justified";
        default:            return "GDP";
    }
}

// [PARTIAL] Of the ten GDP primitives only v_bar is drawn. The rest log and draw
// nothing; v_justified is the one real programs actually reach.
void vdi_gdp(VdiPB *pb) {
    int16_t sub = vdi_pb_contrl(pb, 5);

    if (sub == GDP_BAR) {
        vdi_v_bar(pb);
        return;
    }

    log_write(LOG_ERROR, "%s (GDP sub-function %d) is unimplemented, nothing drawn", vdi_gdp_name(sub), sub);
}

// [FULL]
void vdi_vr_recfl(VdiPB *pb) {
    int16_t x1 = vdi_pb_ptsin(pb, 0);
    int16_t y1 = vdi_pb_ptsin(pb, 1);
    int16_t x2 = vdi_pb_ptsin(pb, 2);
    int16_t y2 = vdi_pb_ptsin(pb, 3);
    fill_rect(x1, y1, x2, y2, "vr_recfl");
}

// LINES & TEXT

// [FULL] Line type and width are unmodeled (see vsl_type/vsl_width), so every polyline is drawn thin and solid.
void vdi_v_pline(VdiPB *pb) {
    int16_t npts = vdi_pb_contrl(pb, 1);
    if (npts < 2) {
        log_write(LOG_API, "v_pline(%d points) -> accepted, nothing to connect", npts);
        return;
    }

    int16_t line_index = vdi_attr_line_color_index();
    COLORREF color = vdi_attr_palette_color(line_index);
    int xor_mode = vdi_attr_write_mode_is_xor();

    int16_t px = vdi_pb_ptsin(pb, 0);
    int16_t py = vdi_pb_ptsin(pb, 1);
    for (int i = 1; i < npts; i++) {
        int16_t x = vdi_pb_ptsin(pb, i * 2);
        int16_t y = vdi_pb_ptsin(pb, i * 2 + 1);
        canvas_line(px, py, x, y, (unsigned int)color, xor_mode);
        px = x;
        py = y;
    }

    log_write(LOG_API, "v_pline(%d points) -> line color index %d%s", npts, line_index, xor_mode ? ", XOR" : "");
}

// [PARTIAL] Strings longer than the local buffer are silently truncated.
void vdi_v_gtext(VdiPB *pb) {
    int16_t x = vdi_pb_ptsin(pb, 0);
    int16_t y = vdi_pb_ptsin(pb, 1);
    int16_t nchars = vdi_pb_contrl(pb, 3);

    char text[128];
    int n = nchars;
    if (n < 0) {
        n = 0;
    } else if (n > (int)sizeof(text) - 1) {
        n = (int)sizeof(text) - 1;
    }
    for (int i = 0; i < n; i++) {
        text[i] = (char)vdi_pb_intin(pb, i);
    }
    text[n] = '\0';

    int16_t text_index = vdi_attr_text_color_index();
    COLORREF color = vdi_attr_palette_color(text_index);
    int16_t effects = vdi_attr_text_effects();
    int opaque = vdi_attr_text_is_opaque();
    int16_t halign = vdi_attr_text_halign();
    int16_t valign = vdi_attr_text_valign();
    canvas_styled_text(x, y, text, (unsigned int)color, effects & TXT_THICKENED, effects & TXT_SKEWED, effects & TXT_UNDERLINED, opaque, (unsigned int)vdi_attr_palette_color(VDI_WHITE), halign, valign);
    log_write(LOG_API, "v_gtext(%d,%d, \"%s\") -> text color index %d, effects 0x%02X, align h=%d v=%d, %s", x, y, text, text_index, effects, halign, valign, opaque ? "opaque cells" : "transparent");
}

// COPY / BLIT

static DWORD vro_cpyfm_rop(int16_t mode) {
    switch (mode) {
        case 0:  return WHITENESS;  // ALL_WHITE: all zero bits, which is palette index 0 (white)
        case 1:  return SRCAND;     // S_AND_D
        case 2:  return SRCERASE;   // S_AND_NOTD
        case 3:  return SRCCOPY;    // S_ONLY (replace mode)
        case 6:  return SRCINVERT;  // S_XOR_D (erase/xor mode)
        case 7:  return SRCPAINT;   // S_OR_D
        case 10: return DSTINVERT;  // NOT_D
        case 12: return NOTSRCCOPY; // NOT_S
        case 15: return BLACKNESS;  // ALL_BLACK: all one bits, which is palette index 1 (black)
        default: return SRCCOPY;    // not exactly modeled, approximated as a plain copy
    }
}

// [PARTIAL] Screen-to-screen only. An MFDB naming off-screen memory is rejected rather
// than blitted, since the canvas is the only surface the layer owns.
void vdi_vro_cpyfm(VdiPB *pb) {
    int16_t mode = vdi_pb_intin(pb, 0);
    int16_t sx1 = vdi_pb_ptsin(pb, 0);
    int16_t sy1 = vdi_pb_ptsin(pb, 1);
    int16_t sx2 = vdi_pb_ptsin(pb, 2);
    int16_t sy2 = vdi_pb_ptsin(pb, 3);
    int16_t dx1 = vdi_pb_ptsin(pb, 4);
    int16_t dy1 = vdi_pb_ptsin(pb, 5);

    unsigned int src_hi = (unsigned int)(uint16_t)vdi_pb_contrl(pb, 7);
    unsigned int src_lo = (unsigned int)(uint16_t)vdi_pb_contrl(pb, 8);
    unsigned int dst_hi = (unsigned int)(uint16_t)vdi_pb_contrl(pb, 9);
    unsigned int dst_lo = (unsigned int)(uint16_t)vdi_pb_contrl(pb, 10);
    unsigned int src_mfdb = (src_hi << 16) | src_lo;
    unsigned int dst_mfdb = (dst_hi << 16) | dst_lo;

    unsigned int src_addr = src_mfdb ? m68k_read_memory_32(src_mfdb + 0) : 0;
    unsigned int dst_addr = dst_mfdb ? m68k_read_memory_32(dst_mfdb + 0) : 0;

    if (src_addr != 0 || dst_addr != 0) {
        log_write(LOG_ERROR, "vro_cpyfm(mode=%d) -> off-screen memory forms are not modeled, nothing blitted (src_addr=0x%08X dst_addr=0x%08X)", mode, src_addr, dst_addr);
        return;
    }

    int16_t w = (int16_t)(sx2 - sx1 + 1);
    int16_t h = (int16_t)(sy2 - sy1 + 1);
    canvas_blit(sx1, sy1, dx1, dy1, w, h, vro_cpyfm_rop(mode));

    log_write(LOG_API, "vro_cpyfm(mode=%d) -> screen blit %d,%d,%d,%d to %d,%d", mode, sx1, sy1, w, h, dx1, dy1);
}