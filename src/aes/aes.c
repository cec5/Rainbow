#include <stdio.h>
#include "m68k.h"
#include "aes/aes.h"
#include "aes/aes_pb.h"
#include "aes/aes_appl.h"
#include "aes/aes_graf.h"
#include "aes/aes_window.h"
#include "aes/aes_event.h"
#include "aes/aes_menu.h"
#include "aes/aes_object.h"
#include "aes/aes_form.h"
#include "aes/aes_rsrc.h"
#include "aes/aes_shel.h"
#include "logger.h"

/* Each handler carries a status tag at its definition: [FULL] behaves as TOS
 * specifies within the layer's scope, [PARTIAL] works but with a stated gap,
 * [NO-OP] is accepted and answered but changes nothing. Calls absent from this
 * table are the fourth case, unimplemented, and are reported by opcode only. */
typedef struct {
    unsigned int opcode;
    const char  *name;
    void       (*handler)(const AesPB *pb);
} AesCall;

static const AesCall s_calls[] = {
    { 0x0A, "appl_init",      aes_appl_init      },
    { 0x13, "appl_exit",      aes_appl_exit      },
    { 0x15, "evnt_button",    aes_evnt_button    },
    { 0x16, "evnt_mouse",     aes_evnt_mouse     },
    { 0x17, "evnt_mesag",     aes_evnt_mesag     },
    { 0x18, "evnt_timer",     aes_evnt_timer     },
    { 0x19, "evnt_multi",     aes_evnt_multi     },
    { 0x1E, "menu_bar",       aes_menu_bar       },
    { 0x1F, "menu_icheck",    aes_menu_icheck    },
    { 0x20, "menu_ienable",   aes_menu_ienable   },
    { 0x21, "menu_tnormal",   aes_menu_tnormal   },
    { 0x22, "menu_text",      aes_menu_text      },
    { 0x2A, "objc_draw",      aes_objc_draw      },
    { 0x2B, "objc_find",      aes_objc_find      },
    { 0x2C, "objc_offset",    aes_objc_offset    },
    { 0x32, "form_do",        aes_form_do        },
    { 0x33, "form_dial",      aes_form_dial      },
    { 0x34, "form_alert",     aes_form_alert     },
    { 0x36, "form_center",    aes_form_center    },
    { 0x46, "graf_rubberbox", aes_graf_rubberbox },
    { 0x49, "graf_growbox",   aes_graf_growbox   },
    { 0x4D, "graf_handle",    aes_graf_handle    },
    { 0x4E, "graf_mouse",     aes_graf_mouse     },
    { 0x4F, "graf_mkstate",   aes_graf_mkstate   },
    { 0x50, "scrp_read",      aes_scrp_read      },
    { 0x51, "scrp_write",     aes_scrp_write     },
    { 0x5A, "fsel_input",     aes_fsel_input     },
    { 0x64, "wind_create",    aes_wind_create    },
    { 0x65, "wind_open",      aes_wind_open      },
    { 0x66, "wind_close",     aes_wind_close     },
    { 0x67, "wind_delete",    aes_wind_delete    },
    { 0x68, "wind_get",       aes_wind_get       },
    { 0x69, "wind_set",       aes_wind_set       },
    { 0x6A, "wind_find",      aes_wind_find      },
    { 0x6B, "wind_update",    aes_wind_update    },
    { 0x6C, "wind_calc",      aes_wind_calc      },
    { 0x6E, "rsrc_load",      aes_rsrc_load      },
    { 0x6F, "rsrc_free",      aes_rsrc_free      },
    { 0x70, "rsrc_gaddr",     aes_rsrc_gaddr     },
    { 0x78, "shel_read",      aes_shel_read      },
    { 0x7C, "shel_find",      aes_shel_find      },
};

#define AES_CALL_COUNT (sizeof(s_calls) / sizeof(s_calls[0]))

static const AesCall *find_call(unsigned int opcode) {
    for (size_t i = 0; i < AES_CALL_COUNT; i++) {
        if (s_calls[i].opcode == opcode) {
            return &s_calls[i];
        }
    }
    return NULL;
}

// Dumps the raw wire data for a call: the first 8 intin words and first 4 addrin longs, regardless of how many the opcode is supposed to use.
static void log_raw_pb(unsigned int opcode, const AesPB *pb) {
    char intin_buf[160];
    int pos = 0;
    for (int i = 0; i < 8 && pos < (int)sizeof(intin_buf) - 8; i++) {
        pos += snprintf(intin_buf + pos, sizeof(intin_buf) - pos, "%s0x%04X", i ? "," : "", m68k_read_memory_16(pb->intin + (unsigned int)i * 2u));
    }

    char addrin_buf[96];
    pos = 0;
    for (int i = 0; i < 4 && pos < (int)sizeof(addrin_buf) - 12; i++) {
        pos += snprintf(addrin_buf + pos, sizeof(addrin_buf) - pos, "%s0x%08X", i ? "," : "", m68k_read_memory_32(pb->addrin + (unsigned int)i * 4u));
    }

    log_write(LOG_API, "AES 0x%02X raw intin=[%s] addrin=[%s]", opcode, intin_buf, addrin_buf);
}

unsigned int aes_dispatch(unsigned int pb_addr) {
    AesPB pb;
    aes_pb_read(pb_addr, &pb);

    unsigned int opcode = m68k_read_memory_16(pb.control);
    const AesCall *call = find_call(opcode);
    const char *name = call ? call->name : "Unknown";

    log_write(LOG_API, "AES %s (0x%02X) called", name, opcode);
    if (logger_is_enabled()) {
        log_raw_pb(opcode, &pb);
    }

    if (call && call->handler) {
        call->handler(&pb);
    } else {
        printf("[aes] Unimplemented AES opcode 0x%02X\n", opcode);
        log_write(LOG_ERROR, "AES opcode 0x%02X is unimplemented", opcode);
    }

    log_write(LOG_API, "AES %s (0x%02X) done", name, opcode);

    // Real AES bindings never read the TRAP #2 return value; results come back through intout[]/addrout[] instead.
    return 1;
}