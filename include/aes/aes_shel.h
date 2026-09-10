#ifndef AES_SHEL_H
#define AES_SHEL_H

#include "aes/aes_pb.h"

// GEM shell interaction: shel_read (launch command line), shel_find (path search).
void aes_shel_read(const AesPB *pb);
void aes_shel_find(const AesPB *pb);

// scrp_read/scrp_write: get/set the clipboard ("scrap") directory path.
void aes_scrp_read(const AesPB *pb);
void aes_scrp_write(const AesPB *pb);

#endif