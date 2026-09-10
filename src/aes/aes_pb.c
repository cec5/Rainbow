#include "m68k.h"
#include "aes/aes_pb.h"

void aes_pb_read(unsigned int pb_addr, AesPB *pb) {
    pb->control = m68k_read_memory_32(pb_addr + 0);
    pb->global  = m68k_read_memory_32(pb_addr + 4);
    pb->intin   = m68k_read_memory_32(pb_addr + 8);
    pb->intout  = m68k_read_memory_32(pb_addr + 12);
    pb->addrin  = m68k_read_memory_32(pb_addr + 16);
    pb->addrout = m68k_read_memory_32(pb_addr + 20);
}

int16_t aes_pb_intin(const AesPB *pb, int index) {
    return (int16_t)m68k_read_memory_16(pb->intin + (unsigned int)index * 2u);
}

unsigned int aes_pb_intin_long(const AesPB *pb, int index) {
    unsigned int hi = m68k_read_memory_16(pb->intin + (unsigned int)index * 2u);
    unsigned int lo = m68k_read_memory_16(pb->intin + (unsigned int)(index + 1) * 2u);
    return (hi << 16) | lo;
}

void aes_pb_set_intout(const AesPB *pb, int index, int16_t value) {
    m68k_write_memory_16(pb->intout + (unsigned int)index * 2u, (unsigned int)(uint16_t)value);
}

unsigned int aes_pb_addrin(const AesPB *pb, int index) {
    return m68k_read_memory_32(pb->addrin + (unsigned int)index * 4u);
}

void aes_pb_set_addrout(const AesPB *pb, int index, unsigned int value) {
    m68k_write_memory_32(pb->addrout + (unsigned int)index * 4u, value);
}

void aes_pb_set_global(const AesPB *pb, int index, int16_t value) {
    m68k_write_memory_16(pb->global + (unsigned int)index * 2u, (unsigned int)(uint16_t)value);
}