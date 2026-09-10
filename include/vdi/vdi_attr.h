#ifndef VDI_ATTR_H
#define VDI_ATTR_H

#include <windows.h>
#include "vdi/vdi_pb.h"

#define VDI_PALETTE_SIZE 16
#define VDI_WHITE     0
#define VDI_BLACK     1
#define VDI_RED       2
#define VDI_GREEN     3
#define VDI_BLUE      4
#define VDI_CYAN      5
#define VDI_YELLOW    6
#define VDI_MAGENTA   7
#define VDI_LWHITE    8
#define VDI_LBLACK    9
#define VDI_LRED     10
#define VDI_LGREEN   11
#define VDI_LBLUE    12
#define VDI_LCYAN    13
#define VDI_LYELLOW  14
#define VDI_LMAGENTA 15

// vst_effects() bit masks. Outlined and shadowed have no GDI equivalent and are ignored.
#define TXT_THICKENED  0x01
#define TXT_LIGHT      0x02
#define TXT_SKEWED     0x04
#define TXT_UNDERLINED 0x08
#define TXT_OUTLINED   0x10
#define TXT_SHADOWED   0x20

/* Line/fill/text/marker attribute setters, plus the vs_color palette. Owns
 * the current fill/line/text color state; vdi_draw.c reads it back through
 * the accessors below when it actually paints. */
void vdi_vsl_type(VdiPB *pb);
void vdi_vsl_width(VdiPB *pb);
void vdi_vsl_udsty(VdiPB *pb);
void vdi_vsm_height(VdiPB *pb);
void vdi_vsf_perimeter(VdiPB *pb);

void vdi_vsl_ends(VdiPB *pb);
void vdi_vst_height(VdiPB *pb);
void vdi_vsf_color(VdiPB *pb);
void vdi_vsf_interior(VdiPB *pb);
void vdi_vswr_mode(VdiPB *pb);
void vdi_vsl_color(VdiPB *pb);
void vdi_vst_color(VdiPB *pb);
void vdi_vst_effects(VdiPB *pb);
void vdi_vst_alignment(VdiPB *pb);
void vdi_vs_color(VdiPB *pb);

int16_t vdi_attr_fill_color_index(void);
COLORREF vdi_attr_fill_rgb(void);
int vdi_attr_write_mode_is_xor(void);
int16_t vdi_attr_text_color_index(void);
int16_t vdi_attr_text_effects(void);
int vdi_attr_text_is_opaque(void);
int16_t vdi_attr_text_halign(void);
int16_t vdi_attr_text_valign(void);
int16_t vdi_attr_line_color_index(void);
COLORREF vdi_attr_palette_color(int16_t index);

#endif