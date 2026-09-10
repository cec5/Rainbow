#ifndef VDI_WORKSTATION_H
#define VDI_WORKSTATION_H

#include "vdi/vdi_pb.h"

// Workstation lifecycle: v_opnvwk, v_clsvwk, v_clrwk, plus mouse cursor visibility.
void vdi_v_opnvwk(VdiPB *pb);
void vdi_v_clsvwk(VdiPB *pb);
void vdi_v_clrwk(VdiPB *pb);
void vdi_v_show_c(VdiPB *pb);
void vdi_v_hide_c(VdiPB *pb);

#endif