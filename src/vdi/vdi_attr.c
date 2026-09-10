#include "vdi/vdi_attr.h"
#include "logger.h"

#define FIS_HOLLOW 0
#define MD_REPLACE 1
#define MD_XOR     3

// PALETTE

// Default ST 16-color palette. Mutable rather than const because vs_color() may overwrite any entry at runtime.
static COLORREF s_palette[VDI_PALETTE_SIZE] = {
    RGB(255, 255, 255), RGB(0, 0, 0),       RGB(255, 0, 0),     RGB(0, 255, 0),
    RGB(0, 0, 255),     RGB(0, 255, 255),   RGB(255, 255, 0),   RGB(255, 0, 255),
    RGB(192, 192, 192), RGB(96, 96, 96),    RGB(255, 128, 128), RGB(128, 255, 128),
    RGB(128, 128, 255), RGB(128, 255, 255), RGB(255, 255, 128), RGB(255, 128, 255),
};

COLORREF vdi_attr_palette_color(int16_t index) {
    if (index < 0 || index >= VDI_PALETTE_SIZE) {
        return RGB(0, 0, 0);
    }
    return s_palette[index];
}

// [FULL] Components arrive on a 0-1000 scale, not 0-255.
void vdi_vs_color(VdiPB *pb) {
    int16_t index = vdi_pb_intin(pb, 0);
    int16_t r = vdi_pb_intin(pb, 1);
    int16_t g = vdi_pb_intin(pb, 2);
    int16_t b = vdi_pb_intin(pb, 3);

    if (index >= 0 && index < VDI_PALETTE_SIZE) {
        s_palette[index] = RGB(r * 255 / 1000, g * 255 / 1000, b * 255 / 1000);
    }

    log_write(LOG_API, "vs_color(%d, %d,%d,%d)", index, r, g, b);
}

// FILL & WRITE MODE

static int16_t s_fill_color = VDI_BLACK;     // GEM's own default
static int16_t s_fill_interior = FIS_HOLLOW; // GEM's own default
static int16_t s_write_mode = MD_REPLACE;

int16_t vdi_attr_fill_color_index(void) {
    return s_fill_color;
}

COLORREF vdi_attr_fill_rgb(void) {
    return vdi_attr_palette_color(s_fill_interior == FIS_HOLLOW ? VDI_WHITE : s_fill_color);
}

int vdi_attr_write_mode_is_xor(void) {
    return s_write_mode == MD_XOR;
}

// [FULL]
void vdi_vsf_color(VdiPB *pb) {
    int16_t color = vdi_pb_intin(pb, 0);
    s_fill_color = color;
    vdi_pb_set_intout(pb, 0, color);
    log_write(LOG_API, "vsf_color(%d)", color);
}

// [FULL] Only hollow-vs-not is honored; the individual pattern styles are not drawn.
void vdi_vsf_interior(VdiPB *pb) {
    int16_t style = vdi_pb_intin(pb, 0);
    s_fill_interior = style;
    vdi_pb_set_intout(pb, 0, style);
    log_write(LOG_API, "vsf_interior(%d) -> %s", style, style == FIS_HOLLOW ? "hollow, fills erase to the background color" : "solid/patterned, fills use the fill color");
}

// [FULL] Only replace and XOR are distinguished; the transparent modes fall in with replace.
void vdi_vswr_mode(VdiPB *pb) {
    int16_t mode = vdi_pb_intin(pb, 0);
    s_write_mode = mode;
    vdi_pb_set_intout(pb, 0, mode);
    log_write(LOG_API, "vswr_mode(%d) -> %s", mode, mode == MD_XOR ? "XOR, fills invert the destination" : "opaque, fills replace the destination");
}

// TEXT & LINE COLOR

static int16_t s_line_color = VDI_BLACK;
static int16_t s_text_color = VDI_BLACK;

int16_t vdi_attr_text_color_index(void) {
    return s_text_color;
}

int16_t vdi_attr_line_color_index(void) {
    return s_line_color;
}

// [FULL]
void vdi_vsl_color(VdiPB *pb) {
    int16_t color = vdi_pb_intin(pb, 0);
    s_line_color = color;
    vdi_pb_set_intout(pb, 0, color);
    log_write(LOG_API, "vsl_color(%d)", color);
}

// [FULL]
void vdi_vst_color(VdiPB *pb) {
    int16_t color = vdi_pb_intin(pb, 0);
    s_text_color = color;
    vdi_pb_set_intout(pb, 0, color);
    log_write(LOG_API, "vst_color(%d)", color);
}

// TEXT EFFECTS

static int16_t s_text_effects = 0;

int16_t vdi_attr_text_effects(void) {
    return s_text_effects;
}

int vdi_attr_text_is_opaque(void) {
    return s_write_mode == MD_REPLACE;
}

// [PARTIAL] Thickened, skewed and underlined reach the canvas; outlined and shadowed are stored and reported back but never drawn.
void vdi_vst_effects(VdiPB *pb) {
    int16_t effects = vdi_pb_intin(pb, 0);
    s_text_effects = effects;
    vdi_pb_set_intout(pb, 0, effects);
    log_write(LOG_API, "vst_effects(0x%02X) ->%s%s%s%s%s%s", effects, (effects & TXT_THICKENED) ? " thickened" : "", (effects & TXT_LIGHT) ? " light" : "", (effects & TXT_SKEWED) ? " skewed" : "", (effects & TXT_UNDERLINED) ? " underlined" : "", (effects & (TXT_OUTLINED | TXT_SHADOWED)) ? " outlined/shadowed (not modeled)" : "", effects ? "" : " normal");
}

// TEXT ALIGNMENT

static int16_t s_text_halign = 0;
static int16_t s_text_valign = 5; // top

int16_t vdi_attr_text_halign(void) {
    return s_text_halign;
}

int16_t vdi_attr_text_valign(void) {
    return s_text_valign;
}

// [FULL] Out-of-range values fall back to the defaults, and intout reports what was actually set rather than what was asked for.
void vdi_vst_alignment(VdiPB *pb) {
    int16_t halign = vdi_pb_intin(pb, 0);
    int16_t valign = vdi_pb_intin(pb, 1);

    s_text_halign = (halign >= 0 && halign <= 2) ? halign : 0;
    s_text_valign = (valign >= 0 && valign <= 5) ? valign : 5;

    vdi_pb_set_intout(pb, 0, s_text_halign);
    vdi_pb_set_intout(pb, 1, s_text_valign);

    log_write(LOG_API, "vst_alignment(h=%d, v=%d) -> set h=%d, v=%d", halign, valign, s_text_halign, s_text_valign);
}

// UNMODELED ATTRIBUTES

// Echoing the value back keeps a guest that checks the return from treating the call as failed, without the attribute affecting anything drawn.
static void echo_single_attr(VdiPB *pb, const char *name) {
    int16_t value = vdi_pb_intin(pb, 0);
    vdi_pb_set_intout(pb, 0, value);
    log_write(LOG_API, "%s(%d) -> accepted, not otherwise modeled", name, value);
}

void vdi_vsl_type(VdiPB *pb)       { echo_single_attr(pb, "vsl_type"); }      // [NO-OP]
void vdi_vsl_width(VdiPB *pb)      { echo_single_attr(pb, "vsl_width"); }     // [NO-OP]
void vdi_vsl_udsty(VdiPB *pb)      { echo_single_attr(pb, "vsl_udsty"); }     // [NO-OP]
void vdi_vsm_height(VdiPB *pb)     { echo_single_attr(pb, "vsm_height"); }    // [NO-OP]
void vdi_vsf_perimeter(VdiPB *pb)  { echo_single_attr(pb, "vsf_perimeter"); } // [NO-OP]

// [NO-OP] Takes two values rather than one, so it cannot share echo_single_attr().
void vdi_vsl_ends(VdiPB *pb) {
    int16_t start = vdi_pb_intin(pb, 0);
    int16_t end = vdi_pb_intin(pb, 1);
    log_write(LOG_API, "vsl_ends(%d, %d) -> accepted, not otherwise modeled", start, end);
}

// [PARTIAL] Answers with plausible metrics, but the canvas font is a fixed size, so text does not actually change with it.
void vdi_vst_height(VdiPB *pb) {
    int16_t height = vdi_pb_ptsin(pb, 1);
    vdi_pb_set_ptsout(pb, 0, (int16_t)(height / 2));
    vdi_pb_set_ptsout(pb, 1, height);
    vdi_pb_set_ptsout(pb, 2, (int16_t)(height / 2 + 1));
    vdi_pb_set_ptsout(pb, 3, (int16_t)(height + 2));
    log_write(LOG_API, "vst_height(%d) -> accepted, approximated metrics", height);
}