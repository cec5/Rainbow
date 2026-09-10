#include <string.h>
#include <windows.h>
#include "aes/aes_object.h"
#include "screen.h"
#include "aes/aes_rsrc.h"
#include "aes/aes_window.h"
#include "vdi/vdi_attr.h"
#include "guest_mem_util.h"
#include "m68k.h"
#include "logger.h"

/* The Object Library: reading an object tree out of guest memory, drawing it,
 * and finding things in it. The form_ calls that run a dialog on top of all
 * this are in aes_form.c. */

// OBJECT ACCESS

unsigned int aes_object_addr(unsigned int tree_base, int index) {
    return tree_base + (unsigned int)index * OBJECT_SIZE;
}

static int obj_index_in_tree(unsigned int tree_base, unsigned int obj_addr) {
    return (int)((obj_addr - tree_base) / OBJECT_SIZE);
}

#define MAX_TREE_OBJECTS 512

int aes_object_tree_limit(unsigned int tree_base) {
    if (tree_base == 0) {
        return 1;
    }

    unsigned int base = aes_rsrc_object_base();
    int nobs = aes_rsrc_object_count();
    if (base != 0 && tree_base >= base && nobs > 0) {
        int first = (int)((tree_base - base) / OBJECT_SIZE);
        if (nobs > first) {
            return nobs - first;
        }
    }

    /* A tree the guest built itself isn't in the resource, so there's no count
     * to look up. GEM trees are self-terminating: the last object carries
     * OF_LASTOB, so walk until it shows up. */
    for (int i = 0; i < MAX_TREE_OBJECTS; i++) {
        unsigned int flags = m68k_read_memory_16(aes_object_addr(tree_base, i) + OBJECT_OFS_FLAGS);
        if (flags & OF_LASTOB) {
            return i + 1;
        }
    }
    return MAX_TREE_OBJECTS;
}

// CHILD ITERATION

static int obj_index_usable(unsigned int index, int nobs) {
    return index != NIL_OBJECT && (int)index < nobs;
}

AesChildIter aes_object_child_run(unsigned int tree_base, unsigned int head, unsigned int tail, int nobs) {
    AesChildIter it;
    it.tree_base = tree_base;
    it.nobs      = nobs;
    it.head      = head;
    it.tail      = tail;
    it.steps     = 0;
    it.index     = obj_index_usable(head, nobs) ? head : NIL_OBJECT;
    it.addr      = (it.index != NIL_OBJECT) ? aes_object_addr(tree_base, (int)it.index) : 0;
    return it;
}

AesChildIter aes_object_child_first(unsigned int tree_base, unsigned int parent_addr, int nobs) {
    return aes_object_child_run(tree_base,
                                m68k_read_memory_16(parent_addr + OBJECT_OFS_HEAD),
                                m68k_read_memory_16(parent_addr + OBJECT_OFS_TAIL),
                                nobs);
}

int aes_object_child_valid(const AesChildIter *it) {
    return it->index != NIL_OBJECT;
}

void aes_object_child_next(AesChildIter *it) {
    if (it->index == NIL_OBJECT) {
        return;
    }
    if (it->index == it->tail) {
        it->index = NIL_OBJECT; // ob_tail names the last child, so the run ends on it rather than after it
        it->addr = 0;
        return;
    }
    unsigned int next = m68k_read_memory_16(it->addr + OBJECT_OFS_NEXT);
    /* No run can be longer than the tree, so a walk still going after that
     * many steps is following an ob_next cycle. The guest owns this memory and
     * can corrupt it, and every one of these walks used to be able to hang the
     * layer outright; bounding it in one place bounds all of them. */
    if (!obj_index_usable(next, it->nobs) || ++it->steps >= it->nobs) {
        it->index = NIL_OBJECT; // malformed tree
        it->addr = 0;
        return;
    }
    it->index = next;
    it->addr = aes_object_addr(it->tree_base, (int)next);
}

int aes_object_rect(unsigned int obj_addr, int16_t ax, int16_t ay, int16_t out[4]) {
    out[0] = ax;
    out[1] = ay;
    out[2] = (int16_t)m68k_read_memory_16(obj_addr + OBJECT_OFS_W);
    out[3] = (int16_t)m68k_read_memory_16(obj_addr + OBJECT_OFS_H);
    return out[2] > 0 && out[3] > 0;
}

// DRAWING

// One colour word serves both ob_spec and TEDINFO's te_color: border in bits 12-15, text in 8-11, fill pattern in 4-6, fill colour in 0-3.
#define COLOR_BORDER(c) ((int16_t)(((c) >> 12) & 0x0F))
#define COLOR_TEXT(c)   ((int16_t)(((c) >> 8) & 0x0F))
#define COLOR_FILL(c)   ((int16_t)((c) & 0x0F))

static void draw_border_ring(int16_t x, int16_t y, int16_t w, int16_t h, int t, unsigned int color) {
    if (t <= 0 || w <= 0 || h <= 0) {
        return;
    }
    if (t * 2 >= w || t * 2 >= h) {
        canvas_fill(x, y, w, h, color); // too thick to leave a hole
        return;
    }
    canvas_fill(x, y, w, (int16_t)t, color);
    canvas_fill(x, (int16_t)(y + h - t), w, (int16_t)t, color);
    canvas_fill(x, (int16_t)(y + t), (int16_t)t, (int16_t)(h - 2 * t), color);
    canvas_fill((int16_t)(x + w - t), (int16_t)(y + t), (int16_t)t, (int16_t)(h - 2 * t), color);
}

static void draw_box(unsigned int spec, int16_t x, int16_t y, int16_t w, int16_t h, unsigned int state, int fill_interior) {
    int thickness = (int)(signed char)((spec >> 16) & 0xFF);
    int t = thickness < 0 ? -thickness : thickness;

    if (fill_interior) {
        unsigned int fill_color = vdi_attr_palette_color(state & OS_SELECTED ? VDI_BLACK : COLOR_FILL(spec));
        canvas_fill(x, y, w, h, fill_color);
    }

    if (t > 0) {
        unsigned int border_color = vdi_attr_palette_color(COLOR_BORDER(spec));
        if (thickness < 0) {
            draw_border_ring((int16_t)(x - t), (int16_t)(y - t), (int16_t)(w + 2 * t), (int16_t)(h + 2 * t), t, border_color);
        } else {
            draw_border_ring(x, y, w, h, t, border_color);
        }
    }
}

static void draw_object_text(const char *text, const int16_t rect[4], int just, unsigned int color, int small_font) {
    int cw = small_font ? AES_SMALL_CHAR_W : AES_CHAR_W;
    int ch = small_font ? AES_SMALL_CHAR_H : AES_CHAR_H;
    int text_w = (int)strlen(text) * cw;

    int16_t tx = rect[0];
    if (just == TE_JUST_RIGHT) {
        tx = (int16_t)(rect[0] + rect[2] - text_w);
    } else if (just == TE_JUST_CENTER) {
        tx = (int16_t)(rect[0] + (rect[2] - text_w) / 2);
    }
    if (tx < rect[0]) {
        tx = rect[0]; // wider than its box; clip on the right rather than spilling left
    }

    int16_t ty = (int16_t)(rect[1] + (rect[3] - ch) / 2);
    if (ty < rect[1]) {
        ty = rect[1];
    }

    canvas_text_font(tx, ty, text, color, small_font);
}

static void draw_object(unsigned int obj_addr, int16_t ax, int16_t ay) {
    unsigned int flags = m68k_read_memory_16(obj_addr + OBJECT_OFS_FLAGS);
    if (flags & OF_HIDETREE) {
        return;
    }

    unsigned int type = m68k_read_memory_16(obj_addr + OBJECT_OFS_TYPE) & 0xFF;
    unsigned int state = m68k_read_memory_16(obj_addr + OBJECT_OFS_STATE);
    unsigned int spec = m68k_read_memory_32(obj_addr + OBJECT_OFS_SPEC);
    int16_t rect[4];
    aes_object_rect(obj_addr, ax, ay, rect);

    switch (type) {
        case G_BOX:
        case G_IBOX:
        case G_BOXCHAR:
            draw_box(spec, rect[0], rect[1], rect[2], rect[3], state, type != G_IBOX);
            if (type == G_BOXCHAR) {
                unsigned int text_color = vdi_attr_palette_color(state & OS_SELECTED ? VDI_WHITE : COLOR_TEXT(spec));
                char ch[2] = {(char)((spec >> 24) & 0xFF), 0};
                draw_object_text(ch, rect, TE_JUST_CENTER, text_color, 0);
            }
            break;

        case G_BUTTON: {
            int selected = (state & OS_SELECTED) != 0;
            canvas_fill(rect[0], rect[1], rect[2], rect[3], vdi_attr_palette_color(selected ? VDI_BLACK : VDI_WHITE));

            draw_border_ring(rect[0], rect[1], rect[2], rect[3], 1, vdi_attr_palette_color(selected ? VDI_WHITE : VDI_BLACK));

            char text[128];
            guest_read_cstring(spec, text, sizeof(text));
            unsigned int text_color = vdi_attr_palette_color(state & OS_DISABLED ? VDI_LWHITE : (selected ? VDI_WHITE : VDI_BLACK));
            draw_object_text(text, rect, TE_JUST_CENTER, text_color, 0);
            break;
        }

        case G_STRING:
        case G_TITLE: {
            char text[256];
            guest_read_cstring(spec, text, sizeof(text));
            unsigned int text_color = vdi_attr_palette_color(state & OS_DISABLED ? VDI_LWHITE : VDI_BLACK);
            draw_object_text(text, rect, TE_JUST_LEFT, text_color, 0);
            break;
        }

        case G_TEXT:
        case G_BOXTEXT:
        case G_FTEXT:
        case G_FBOXTEXT: {
            unsigned int ptext = m68k_read_memory_32(spec + TE_OFS_PTEXT);
            unsigned int te_color = m68k_read_memory_16(spec + TE_OFS_COLOR);
            int just = (int)(int16_t)m68k_read_memory_16(spec + TE_OFS_JUST);
            int small_font = (int16_t)m68k_read_memory_16(spec + TE_OFS_FONT) == TE_FONT_SMALL;

            if (type == G_BOXTEXT || type == G_FBOXTEXT) {
                canvas_fill(rect[0], rect[1], rect[2], rect[3], vdi_attr_palette_color(state & OS_SELECTED ? VDI_BLACK : COLOR_FILL(te_color)));
            }

            char text[256];
            guest_read_cstring(ptext, text, sizeof(text));
            draw_object_text(text, rect, just, vdi_attr_palette_color(state & OS_SELECTED ? VDI_WHITE : COLOR_TEXT(te_color)), small_font);
            break;
        }

        case G_ICON:
        case G_CICON:
        case G_IMAGE:
            // The object still occupies its rect and its children still get walked below; only the bitmap is missing.
            log_write(LOG_ERROR, "objc_draw: object type %u (icon/image) is unimplemented, no bitmap drawn at %d,%d,%d,%d", type, rect[0], rect[1], rect[2], rect[3]);
            break;

        default:
            log_write(LOG_ERROR, "objc_draw: object type %u is unimplemented, nothing drawn at %d,%d,%d,%d", type, rect[0], rect[1], rect[2], rect[3]);
            break;
    }

    if (state & OS_OUTLINED) {
        unsigned int outline = vdi_attr_palette_color(VDI_BLACK);
        canvas_fill(rect[0], rect[1], rect[2], 1, outline);
        canvas_fill(rect[0], rect[1] + rect[3] - 1, rect[2], 1, outline);
        canvas_fill(rect[0], rect[1], 1, rect[3], outline);
        canvas_fill(rect[0] + rect[2] - 1, rect[1], 1, rect[3], outline);
    }
}

static void walk_tree(unsigned int tree_base, unsigned int obj_addr, int16_t ax, int16_t ay, int nobs, int depth_remaining) {
    draw_object(obj_addr, ax, ay);

    if (depth_remaining <= 0) {
        return;
    }

    for (AesChildIter it = aes_object_child_first(tree_base, obj_addr, nobs); aes_object_child_valid(&it); aes_object_child_next(&it)) {
        int16_t cx = ax + (int16_t)m68k_read_memory_16(it.addr + OBJECT_OFS_X);
        int16_t cy = ay + (int16_t)m68k_read_memory_16(it.addr + OBJECT_OFS_Y);
        walk_tree(tree_base, it.addr, cx, cy, nobs, depth_remaining - 1);
    }
}


int aes_objc_draw_at(unsigned int tree_base, unsigned int start_addr, int16_t at_x, int16_t at_y, int16_t clip_x, int16_t clip_y, int16_t clip_w, int16_t clip_h, int16_t depth) {
    if (start_addr == 0 || clip_w <= 0 || clip_h <= 0) {
        return 0;
    }

    aes_window_save_clip();
    if (!aes_window_set_clip(clip_x, clip_y, clip_w, clip_h)) {
        aes_window_restore_clip();
        return 0;
    }

    walk_tree(tree_base, start_addr, at_x, at_y, aes_object_tree_limit(tree_base), depth > 0 ? depth : 1);

    aes_window_clear_clip();
    aes_window_restore_clip();
    return 1;
}

// [FULL] A start object other than the root has its absolute position resolved first, since ob_x/ob_y are parent-relative.
void aes_objc_draw(const AesPB *pb) {
    unsigned int tree = aes_pb_addrin(pb, 0);
    int16_t start_obj = aes_pb_intin(pb, 0);
    int16_t depth = aes_pb_intin(pb, 1);
    int16_t clip_x = aes_pb_intin(pb, 2);
    int16_t clip_y = aes_pb_intin(pb, 3);
    int16_t clip_w = aes_pb_intin(pb, 4);
    int16_t clip_h = aes_pb_intin(pb, 5);

    aes_pb_set_intout(pb, 0, 1);

    unsigned int start_addr = aes_object_addr(tree, start_obj);

    int16_t at_x = (int16_t)m68k_read_memory_16(tree + OBJECT_OFS_X);
    int16_t at_y = (int16_t)m68k_read_memory_16(tree + OBJECT_OFS_Y);
    if (start_obj != 0) {
        aes_object_abs_pos(tree, tree, at_x, at_y, aes_object_tree_limit(tree), start_obj, &at_x, &at_y);
    }

    int drawn = aes_objc_draw_at(tree, start_addr, at_x, at_y, clip_x, clip_y, clip_w, clip_h, depth);

    log_write(LOG_API, "objc_draw(tree=0x%08X, start=%d, depth=%d) -> %s at %d,%d clipped to %d,%d,%d,%d", tree, start_obj, depth, drawn ? "drawn" : "accepted, no window to draw into", at_x, at_y, clip_x, clip_y, clip_w, clip_h);
}

// TREE SEARCH

int aes_object_abs_pos(unsigned int tree_base, unsigned int obj_addr, int16_t ax, int16_t ay, int nobs, int target_idx, int16_t *out_ax, int16_t *out_ay) {
    if (obj_index_in_tree(tree_base, obj_addr) == target_idx) {
        *out_ax = ax;
        *out_ay = ay;
        return 1;
    }

    for (AesChildIter it = aes_object_child_first(tree_base, obj_addr, nobs); aes_object_child_valid(&it); aes_object_child_next(&it)) {
        int16_t cx = ax + (int16_t)m68k_read_memory_16(it.addr + OBJECT_OFS_X);
        int16_t cy = ay + (int16_t)m68k_read_memory_16(it.addr + OBJECT_OFS_Y);
        if (aes_object_abs_pos(tree_base, it.addr, cx, cy, nobs, target_idx, out_ax, out_ay)) {
            return 1;
        }
    }
    return 0;
}

int aes_object_find_flagged(unsigned int tree_base, unsigned int obj_addr, int nobs, unsigned int flag_mask) {
    unsigned int flags = m68k_read_memory_16(obj_addr + OBJECT_OFS_FLAGS);
    if (flags & flag_mask) {
        return obj_index_in_tree(tree_base, obj_addr);
    }

    for (AesChildIter it = aes_object_child_first(tree_base, obj_addr, nobs); aes_object_child_valid(&it); aes_object_child_next(&it)) {
        int found = aes_object_find_flagged(tree_base, it.addr, nobs, flag_mask);
        if (found >= 0) {
            return found;
        }
    }
    return -1;
}

int aes_object_find_at(unsigned int tree_base, unsigned int obj_addr, int16_t ax, int16_t ay, int16_t px, int16_t py, int nobs, int depth_remaining, int *out_parent_head, int *out_parent_tail) {
    unsigned int flags = m68k_read_memory_16(obj_addr + OBJECT_OFS_FLAGS);
    if (flags & OF_HIDETREE) {
        return -1;
    }

    if (depth_remaining > 0) {
        for (AesChildIter it = aes_object_child_first(tree_base, obj_addr, nobs); aes_object_child_valid(&it); aes_object_child_next(&it)) {
            int16_t cx = ax + (int16_t)m68k_read_memory_16(it.addr + OBJECT_OFS_X);
            int16_t cy = ay + (int16_t)m68k_read_memory_16(it.addr + OBJECT_OFS_Y);

            int hit = aes_object_find_at(tree_base, it.addr, cx, cy, px, py, nobs, depth_remaining - 1, out_parent_head, out_parent_tail);
            if (hit >= 0) {
                // The hit is one of this object's own children, so this is the group a radio button would belong to.
                if (hit == (int)it.index && out_parent_head && out_parent_tail) {
                    *out_parent_head = (int)it.head;
                    *out_parent_tail = (int)it.tail;
                }
                return hit;
            }
        }
    }

    if (flags & OF_SELECTABLE) {
        int16_t rect[4];
        aes_object_rect(obj_addr, ax, ay, rect);
        if (rect[2] > 0 && rect[3] > 0 &&
            px >= rect[0] && px < rect[0] + rect[2] &&
            py >= rect[1] && py < rect[1] + rect[3]) {
            return obj_index_in_tree(tree_base, obj_addr);
        }
    }

    return -1;
}

// [FULL] Searches children before parents, so the innermost object under the point wins.
void aes_objc_find(const AesPB *pb) {
    unsigned int tree = aes_pb_addrin(pb, 0);
    int16_t start_obj = aes_pb_intin(pb, 0);
    int16_t depth = aes_pb_intin(pb, 1);
    int16_t px = aes_pb_intin(pb, 2);
    int16_t py = aes_pb_intin(pb, 3);

    unsigned int start_addr = aes_object_addr(tree, start_obj);
    int nobs = aes_object_tree_limit(tree);

    int16_t ax = (int16_t)m68k_read_memory_16(tree + OBJECT_OFS_X);
    int16_t ay = (int16_t)m68k_read_memory_16(tree + OBJECT_OFS_Y);
    if (start_obj != 0) {
        aes_object_abs_pos(tree, tree, ax, ay, nobs, start_obj, &ax, &ay);
    }

    int found = aes_object_find_at(tree, start_addr, ax, ay, px, py, nobs, depth > 0 ? depth : 8, NULL, NULL);

    aes_pb_set_intout(pb, 0, (int16_t)found);

    log_write(LOG_API, "objc_find(tree=0x%08X, start=%d, depth=%d, pt=%d,%d) -> object %d", tree, start_obj, depth, px, py, found);
}

// [FULL] objc_offset()
void aes_objc_offset(const AesPB *pb) {
    unsigned int tree = aes_pb_addrin(pb, 0);
    int16_t obj = aes_pb_intin(pb, 0);

    int16_t root_x = (int16_t)m68k_read_memory_16(tree + OBJECT_OFS_X);
    int16_t root_y = (int16_t)m68k_read_memory_16(tree + OBJECT_OFS_Y);

    int16_t ox = root_x, oy = root_y;
    aes_object_abs_pos(tree, tree, root_x, root_y, aes_object_tree_limit(tree), obj, &ox, &oy);

    aes_pb_set_intout(pb, 0, 1);
    aes_pb_set_intout(pb, 1, ox);
    aes_pb_set_intout(pb, 2, oy);

    log_write(LOG_API, "objc_offset(tree=0x%08X, obj=%d) -> %d,%d", tree, obj, ox, oy);
}