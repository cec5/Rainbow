#ifndef AES_EVENT_H
#define AES_EVENT_H

#include "aes/aes_pb.h"

void aes_evnt_mesag(const AesPB *pb);
void aes_evnt_multi(const AesPB *pb);

void aes_evnt_button(const AesPB *pb);
void aes_evnt_timer(const AesPB *pb);
void aes_evnt_mouse(const AesPB *pb);

#endif