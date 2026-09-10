#ifndef AES_RSRC_H
#define AES_RSRC_H

#include "aes/aes_pb.h"

void aes_rsrc_load(const AesPB *pb);
void aes_rsrc_gaddr(const AesPB *pb);
void aes_rsrc_free(const AesPB *pb);

unsigned int aes_rsrc_object_base(void);
int aes_rsrc_object_count(void);

#endif