#ifndef VDI_DRAW_H
#define VDI_DRAW_H

#include "vdi/vdi_pb.h"

// GDP sub-functions, selected by contrl[5] under the single opcode 11.
#define GDP_BAR       1
#define GDP_ARC       2
#define GDP_PIESLICE  3
#define GDP_CIRCLE    4
#define GDP_ELLIPSE   5
#define GDP_ELLARC    6
#define GDP_ELLPIE    7
#define GDP_RBOX      8
#define GDP_RFBOX     9
#define GDP_JUSTIFIED 10

void vdi_vs_clip(VdiPB *pb);
void vdi_gdp(VdiPB *pb);
const char *vdi_gdp_name(int16_t sub);
void vdi_vr_recfl(VdiPB *pb);
void vdi_v_pline(VdiPB *pb);
void vdi_v_gtext(VdiPB *pb);
void vdi_vro_cpyfm(VdiPB *pb);

#endif