#ifndef AES_OBJECT_H
#define AES_OBJECT_H

#include "aes/aes_pb.h"

#define G_BOX      20
#define G_TEXT     21
#define G_BOXTEXT  22
#define G_IMAGE    23
#define G_PROGDEF  24
#define G_IBOX     25
#define G_BUTTON   26
#define G_BOXCHAR  27
#define G_STRING   28
#define G_FTEXT    29
#define G_FBOXTEXT 30
#define G_ICON     31
#define G_TITLE    32
#define G_CICON    33

// OBJECT field offsets (24 bytes: ob_next,ob_head,ob_tail WORDx3, ob_type,ob_flags,ob_state WORDx3, ob_spec LONG, ob_x,ob_y,ob_w,ob_h WORDx4).
#define OBJECT_SIZE      24
#define OBJECT_OFS_NEXT   0
#define OBJECT_OFS_HEAD   2
#define OBJECT_OFS_TAIL   4
#define OBJECT_OFS_TYPE   6
#define OBJECT_OFS_FLAGS  8
#define OBJECT_OFS_STATE 10
#define OBJECT_OFS_SPEC  12
#define OBJECT_OFS_X     16
#define OBJECT_OFS_Y     18
#define OBJECT_OFS_W     20
#define OBJECT_OFS_H     22

#define NIL_OBJECT 0xFFFF // terminates an ob_head/ob_tail/ob_next chain

#define OF_SELECTABLE 0x0001
#define OF_DEFAULT    0x0002
#define OF_EXIT       0x0004
#define OF_EDITABLE   0x0008
#define OF_RBUTTON    0x0010
#define OF_LASTOB     0x0020
#define OF_TOUCHEXIT  0x0040
#define OF_HIDETREE   0x0080

#define OS_SELECTED 0x0001
#define OS_CHECKED  0x0004
#define OS_DISABLED 0x0008
#define OS_OUTLINED 0x0010

// TEDINFO field offsets (28 bytes: te_ptext/te_ptmplt/te_pvalid LONGx3, then 8 WORDs).
#define TE_OFS_PTEXT  0
#define TE_OFS_FONT   12
#define TE_OFS_JUST   16
#define TE_OFS_COLOR  18
#define TE_OFS_TXTLEN 24
#define TE_OFS_TMPLEN 26

#define TE_JUST_LEFT   0
#define TE_JUST_RIGHT  1
#define TE_JUST_CENTER 2

#define TE_FONT_SMALL 5

// Every index a guest passes is relative to its own tree, not an absolute index into the resource's whole object array.
unsigned int aes_object_addr(unsigned int tree_base, int index);

// Loose upper bound on how far an index inside `tree_base` can legitimately reach, derived from the loaded resource's object count.
int aes_object_tree_limit(unsigned int tree_base);

typedef struct {
    unsigned int tree_base;
    int          nobs;
    unsigned int head;  // as read from the parent, before validation
    unsigned int tail;
    unsigned int index; // current child; NIL_OBJECT once the run is finished
    unsigned int addr;  // guest address of the current child
    int          steps; // bounds the walk, so a corrupt ob_next cycle ends it instead of hanging
} AesChildIter;

AesChildIter aes_object_child_first(unsigned int tree_base, unsigned int parent_addr, int nobs);

// Same walk over a run whose ends the caller already holds, as the radio-button group in form_do() does.
AesChildIter aes_object_child_run(unsigned int tree_base, unsigned int head, unsigned int tail, int nobs);

int  aes_object_child_valid(const AesChildIter *it);
void aes_object_child_next(AesChildIter *it);

// Fills out[] with x,y,w,h for an object placed at an already-resolved absolute position. Returns non-zero if it has a drawable extent.
int aes_object_rect(unsigned int obj_addr, int16_t ax, int16_t ay, int16_t out[4]);

int aes_object_abs_pos(unsigned int tree_base, unsigned int obj_addr, int16_t ax, int16_t ay, int nobs, int target_idx, int16_t *out_ax, int16_t *out_ay);

int aes_object_find_at(unsigned int tree_base, unsigned int obj_addr, int16_t ax, int16_t ay, int16_t px, int16_t py, int nobs, int depth_remaining, int *out_parent_head, int *out_parent_tail);

// First object in the tree carrying any of `flag_mask` in ob_flags, or -1.
int aes_object_find_flagged(unsigned int tree_base, unsigned int obj_addr, int nobs, unsigned int flag_mask);

void aes_objc_draw(const AesPB *pb);
void aes_objc_find(const AesPB *pb);
void aes_objc_offset(const AesPB *pb);

int aes_objc_draw_at(unsigned int tree_base, unsigned int start_addr, int16_t at_x, int16_t at_y, int16_t clip_x, int16_t clip_y, int16_t clip_w, int16_t clip_h, int16_t depth);

#endif