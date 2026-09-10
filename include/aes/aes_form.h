#ifndef AES_FORM_H
#define AES_FORM_H

#include "aes/aes_pb.h"

void aes_form_do(const AesPB *pb);
void aes_form_dial(const AesPB *pb);
void aes_form_center(const AesPB *pb);
void aes_form_alert(const AesPB *pb);
void aes_fsel_input(const AesPB *pb);

#endif