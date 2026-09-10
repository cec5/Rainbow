#ifndef VDI_QUERY_H
#define VDI_QUERY_H

#include "vdi/vdi_pb.h"

/* Workstation/font capability queries: vq_color, vq_extnd, vqt_fontinfo.
 * These report back what the "hardware" supports rather than changing any
 * state, so they're grouped separately from vdi_attr.c and vdi_workstation.c. */
void vdi_vq_color(VdiPB *pb);
void vdi_vq_extnd(VdiPB *pb);
void vdi_vqt_fontinfo(VdiPB *pb);

#endif