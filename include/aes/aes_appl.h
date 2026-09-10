#ifndef AES_APPL_H
#define AES_APPL_H

#include "aes/aes_pb.h"

// appl_init(): returns an application ID via intout[0]. We only ever run one guest program, so this is always 1.
void aes_appl_init(const AesPB *pb);

void aes_appl_exit(const AesPB *pb);

#endif