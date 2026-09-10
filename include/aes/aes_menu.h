#ifndef AES_MENU_H
#define AES_MENU_H

#include <stdint.h>
#include "aes/aes_pb.h"

// Drops the menu the click landed on and tracks it to a selection, queued as MN_SELECTED. The AES owns the menu bar, not the application.
int aes_menu_track(int16_t x, int16_t y);

void aes_menu_tnormal(const AesPB *pb);
void aes_menu_icheck(const AesPB *pb);
void aes_menu_ienable(const AesPB *pb);
void aes_menu_bar(const AesPB *pb);
void aes_menu_text(const AesPB *pb);

#endif