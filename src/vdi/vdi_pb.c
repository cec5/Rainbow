#include "m68k.h"
#include "vdi/vdi_pb.h"

void vdi_pb_read(unsigned int pb_addr, VdiPB *pb) {
    pb->contrl = m68k_read_memory_32(pb_addr + 0);
    pb->intin  = m68k_read_memory_32(pb_addr + 4);
    pb->ptsin  = m68k_read_memory_32(pb_addr + 8);
    pb->intout = m68k_read_memory_32(pb_addr + 12);
    pb->ptsout = m68k_read_memory_32(pb_addr + 16);
}

int16_t vdi_pb_contrl(const VdiPB *pb, int index) {
    return (int16_t)m68k_read_memory_16(pb->contrl + (unsigned int)index * 2u);
}

void vdi_pb_set_contrl(const VdiPB *pb, int index, int16_t value) {
    m68k_write_memory_16(pb->contrl + (unsigned int)index * 2u, (unsigned int)(uint16_t)value);
}

int16_t vdi_pb_intin(const VdiPB *pb, int index) {
    return (int16_t)m68k_read_memory_16(pb->intin + (unsigned int)index * 2u);
}

int16_t vdi_pb_ptsin(const VdiPB *pb, int index) {
    return (int16_t)m68k_read_memory_16(pb->ptsin + (unsigned int)index * 2u);
}

void vdi_pb_set_intout(const VdiPB *pb, int index, int16_t value) {
    m68k_write_memory_16(pb->intout + (unsigned int)index * 2u, (unsigned int)(uint16_t)value);
}

void vdi_pb_set_ptsout(const VdiPB *pb, int index, int16_t value) {
    m68k_write_memory_16(pb->ptsout + (unsigned int)index * 2u, (unsigned int)(uint16_t)value);
}