#ifndef AES_GRAF_H
#define AES_GRAF_H

#include "aes/aes_pb.h"

void aes_graf_handle(const AesPB *pb);
void aes_graf_mkstate(const AesPB *pb);
void aes_graf_mouse(const AesPB *pb);
void aes_graf_rubberbox(const AesPB *pb);
void aes_graf_growbox(const AesPB *pb);

#endif