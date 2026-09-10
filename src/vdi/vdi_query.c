#include <windows.h>
#include "vdi/vdi_query.h"
#include "vdi/vdi_attr.h"
#include "logger.h"
#include "screen.h"

// [FULL] Reports whatever vs_color() last wrote, converted back to VDI's 0-1000 scale.
void vdi_vq_color(VdiPB *pb) {
    int16_t index = vdi_pb_intin(pb, 0);
    int16_t set_flag = vdi_pb_intin(pb, 1);

    COLORREF color = vdi_attr_palette_color(index);
    int16_t r = (int16_t)(GetRValue(color) * 1000 / 255);
    int16_t g = (int16_t)(GetGValue(color) * 1000 / 255);
    int16_t b = (int16_t)(GetBValue(color) * 1000 / 255);

    vdi_pb_set_intout(pb, 0, index);
    vdi_pb_set_intout(pb, 1, r);
    vdi_pb_set_intout(pb, 2, g);
    vdi_pb_set_intout(pb, 3, b);

    log_write(LOG_API, "vq_color(index=%d, set=%d) -> r=%d,g=%d,b=%d (0-1000 scale)", index, set_flag, r, g, b);
}

// [PARTIAL] Describes the canvas rather than any real ST mode, and only the entries a guest is likely to branch on are populated.
void vdi_vq_extnd(VdiPB *pb) {
    int16_t flag = vdi_pb_intin(pb, 0);

    int screen_w = GUEST_SCREEN_W;
    int screen_h = GUEST_SCREEN_H;

    for (int i = 0; i < 45; i++) {
        vdi_pb_set_intout(pb, i, 0);
    }

    if (flag == 0) {
        vdi_pb_set_intout(pb, 0, (int16_t)(screen_w - 1));  // rightmost pixel
        vdi_pb_set_intout(pb, 1, (int16_t)(screen_h - 1));  // bottommost pixel
        vdi_pb_set_intout(pb, 2, 16);                       // # pen colors
        vdi_pb_set_intout(pb, 3, 16);                       // # colors available
        vdi_pb_set_intout(pb, 13, 16);                      // # of color indices
        vdi_pb_set_intout(pb, 26, 8);                       // smallest char box width
        vdi_pb_set_intout(pb, 27, 16);                      // smallest char box height
        vdi_pb_set_intout(pb, 28, 8);                       // largest char box width
        vdi_pb_set_intout(pb, 29, 16);                      // largest char box height

        log_write(LOG_API, "vq_extnd(flag=0) -> approximated capability array (%dx%d, 16 colors)", screen_w, screen_h);
        return;
    }

    vdi_pb_set_intout(pb, 4, 4);    // bit planes (4 -> the 16 colors reported above)
    vdi_pb_set_intout(pb, 9, 4);    // number of writing modes
    vdi_pb_set_intout(pb, 14, -1);  // max ptsin vertices, -1 = unlimited
    vdi_pb_set_intout(pb, 15, -1);  // max intin words, -1 = unlimited
    vdi_pb_set_intout(pb, 16, 2);   // mouse buttons

    log_write(LOG_API, "vq_extnd(flag=1) -> extended info (4 planes, unlimited ptsin/intin)");
}

// [PARTIAL] Reports the canvas's fixed 8x16 cell whatever font was asked for, since vst_font() is unimplemented and only the one font exists.
void vdi_vqt_fontinfo(VdiPB *pb) {
    int16_t flags = vdi_pb_intin(pb, 0);

    vdi_pb_set_intout(pb, 0, 32);  // first ADE (character code) in font
    vdi_pb_set_intout(pb, 1, 255); // last ADE in font

    vdi_pb_set_ptsout(pb, 0, 0);  // left offset, thickened characters
    vdi_pb_set_ptsout(pb, 1, 0);  // right offset, thickened characters
    vdi_pb_set_ptsout(pb, 2, 0);  // unused
    vdi_pb_set_ptsout(pb, 3, 0);  // unused
    vdi_pb_set_ptsout(pb, 4, 0);  // top offset, slanted/italic characters
    vdi_pb_set_ptsout(pb, 5, 0);  // bottom offset, slanted/italic characters
    vdi_pb_set_ptsout(pb, 6, 16); // character cell height
    vdi_pb_set_ptsout(pb, 7, 0);  // bottom of descender
    vdi_pb_set_ptsout(pb, 8, 13); // top of ascender
    vdi_pb_set_ptsout(pb, 9, 1);  // underline thickness

    log_write(LOG_API, "vqt_fontinfo(flags=%d) -> approximated fixed 8x16 font, ADE 32-255", flags);
}