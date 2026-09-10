#ifndef AES_PB_H
#define AES_PB_H

#include <stdint.h>

// The AES parameter block: six pointers to arrays the guest fills in (control/global/intin/addrin) or reads results from (intout/addrout).
typedef struct {
    unsigned int control;
    unsigned int global;
    unsigned int intin;
    unsigned int intout;
    unsigned int addrin;
    unsigned int addrout;
} AesPB;

void aes_pb_read(unsigned int pb_addr, AesPB *pb);

int16_t aes_pb_intin(const AesPB *pb, int index);
unsigned int aes_pb_intin_long(const AesPB *pb, int index);
void aes_pb_set_intout(const AesPB *pb, int index, int16_t value);
unsigned int aes_pb_addrin(const AesPB *pb, int index);
void aes_pb_set_addrout(const AesPB *pb, int index, unsigned int value);
void aes_pb_set_global(const AesPB *pb, int index, int16_t value);

#endif