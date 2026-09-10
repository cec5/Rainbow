#ifndef VDI_PB_H
#define VDI_PB_H

#include <stdint.h>

/* VDI's parameter block is 5 pointers (contrl/intin/ptsin/intout/ptsout),
 * unlike AES's 6 (which also has global/addrin/addrout); a different array
 * layout for a similar wire protocol. */
typedef struct {
    unsigned int contrl;
    unsigned int intin;
    unsigned int ptsin;
    unsigned int intout;
    unsigned int ptsout;
} VdiPB;

void vdi_pb_read(unsigned int pb_addr, VdiPB *pb);

int16_t vdi_pb_contrl(const VdiPB *pb, int index);
void vdi_pb_set_contrl(const VdiPB *pb, int index, int16_t value);
int16_t vdi_pb_intin(const VdiPB *pb, int index);
int16_t vdi_pb_ptsin(const VdiPB *pb, int index);
void vdi_pb_set_intout(const VdiPB *pb, int index, int16_t value);
void vdi_pb_set_ptsout(const VdiPB *pb, int index, int16_t value);

#endif