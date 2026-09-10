#include <windows.h>
#include "vdi/vdi_workstation.h"
#include "logger.h"
#include "screen.h"

static unsigned int s_vdi_handle = 1;

// [PARTIAL] Only the fields a guest is likely to sanity-check are filled in.
// Must agree with vq_extnd(0), which reports the same array.
void vdi_v_opnvwk(VdiPB *pb) {
    int16_t phys_handle = vdi_pb_contrl(pb, 6);
    s_vdi_handle = (phys_handle != 0) ? (unsigned int)(uint16_t)phys_handle : 1u;
    vdi_pb_set_contrl(pb, 6, (int16_t)s_vdi_handle);

    vdi_pb_set_intout(pb, 0, (int16_t)(GUEST_SCREEN_W - 1));
    vdi_pb_set_intout(pb, 1, (int16_t)(GUEST_SCREEN_H - 1));
    vdi_pb_set_intout(pb, 13, 16);

    log_write(LOG_API, "v_opnvwk() -> handle=%u", s_vdi_handle);
}

// [NO-OP] The canvas outlives every workstation, so there is nothing to tear down.
void vdi_v_clsvwk(VdiPB *pb) {
    (void)pb;
    log_write(LOG_API, "v_clsvwk(handle=%u) -> ok", s_vdi_handle);
}

// [NO-OP] WM_PAINT already clears the canvas before each redraw.
void vdi_v_clrwk(VdiPB *pb) {
    (void)pb;
    log_write(LOG_API, "v_clrwk(handle=%u) -> no-op (WM_PAINT always clears first)", s_vdi_handle);
}

// [NO-OP] Cursor hiding is deliberately disabled: a guest that hides the pointer for
// long stretches leaves the host user without one. Calls kept commented, not deleted,
// so the original behavior is easy to restore.
void vdi_v_show_c(VdiPB *pb) {
    int16_t reset = vdi_pb_intin(pb, 0);
    // if (reset == 0) { while (ShowCursor(TRUE) < 0) {} } else { ShowCursor(TRUE); }
    log_write(LOG_API, "v_show_c(reset=%d) -> accepted, cursor was never hidden", reset);
}

// [NO-OP] See vdi_v_show_c().
void vdi_v_hide_c(VdiPB *pb) {
    (void)pb;
    // ShowCursor(FALSE);
    log_write(LOG_API, "v_hide_c() -> accepted, cursor left visible (hiding disabled)");
}