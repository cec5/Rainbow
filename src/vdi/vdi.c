#include <stdio.h>
#include "vdi/vdi.h"
#include "vdi/vdi_pb.h"
#include "vdi/vdi_workstation.h"
#include "vdi/vdi_attr.h"
#include "vdi/vdi_draw.h"
#include "vdi/vdi_query.h"
#include "logger.h"

// The one opcode whose name depends on more than itself: contrl[5] selects which GDP primitive is meant.
#define VDI_GDP 0x0B

/* Each handler carries a status tag at its definition: [FULL] behaves as TOS
 * specifies within the layer's scope, [PARTIAL] works but with a stated gap,
 * [NO-OP] is accepted and answered but changes nothing. A NULL handler here is
 * the fourth case: unimplemented, named only so the log can report it. */
typedef struct {
    unsigned int opcode;
    const char  *name;
    void       (*handler)(VdiPB *pb);
} VdiCall;

static const VdiCall s_calls[] = {
    { 0x02, "v_clswk",       NULL              },
    { 0x03, "v_clrwk",       vdi_v_clrwk       },
    { 0x06, "v_pline",       vdi_v_pline       },
    { 0x08, "v_gtext",       vdi_v_gtext       },
    { 0x09, "v_fillarea",    NULL              },
    { 0x0B, "v_gdp",         vdi_gdp           }, // name resolved from contrl[5] instead, below
    { 0x0C, "vst_height",    vdi_vst_height    },
    { 0x0D, "vst_rotation",  NULL              },
    { 0x0E, "vs_color",      vdi_vs_color      },
    { 0x0F, "vsl_type",      vdi_vsl_type      },
    { 0x10, "vsl_width",     vdi_vsl_width     },
    { 0x11, "vsl_color",     vdi_vsl_color     },
    { 0x13, "vsm_height",    vdi_vsm_height    },
    { 0x15, "vst_font",      NULL              },
    { 0x16, "vst_color",     vdi_vst_color     },
    { 0x17, "vsf_interior",  vdi_vsf_interior  },
    { 0x19, "vsf_color",     vdi_vsf_color     },
    { 0x1A, "vq_color",      vdi_vq_color      },
    { 0x20, "vswr_mode",     vdi_vswr_mode     },
    { 0x27, "vst_alignment", vdi_vst_alignment },
    { 0x64, "v_opnvwk",      vdi_v_opnvwk      },
    { 0x65, "v_clsvwk",      vdi_v_clsvwk      },
    { 0x66, "vq_extnd",      vdi_vq_extnd      },
    { 0x68, "vsf_perimeter", vdi_vsf_perimeter },
    { 0x6A, "vst_effects",   vdi_vst_effects   },
    { 0x6B, "vst_point",     NULL              },
    { 0x6C, "vsl_ends",      vdi_vsl_ends      },
    { 0x6D, "vro_cpyfm",     vdi_vro_cpyfm     },
    { 0x71, "vsl_udsty",     vdi_vsl_udsty     },
    { 0x72, "vr_recfl",      vdi_vr_recfl      },
    { 0x74, "vqt_extent",    NULL              },
    { 0x79, "vrt_cpyfm",     NULL              },
    { 0x7A, "v_show_c",      vdi_v_show_c      },
    { 0x7B, "v_hide_c",      vdi_v_hide_c      },
    { 0x81, "vs_clip",       vdi_vs_clip       },
    { 0x83, "vqt_fontinfo",  vdi_vqt_fontinfo  },
};

#define VDI_CALL_COUNT (sizeof(s_calls) / sizeof(s_calls[0]))

static const VdiCall *find_call(unsigned int opcode) {
    for (size_t i = 0; i < VDI_CALL_COUNT; i++) {
        if (s_calls[i].opcode == opcode) {
            return &s_calls[i];
        }
    }
    return NULL;
}

void vdi_dispatch(unsigned int pb_addr) {
    VdiPB pb;
    vdi_pb_read(pb_addr, &pb);

    unsigned int opcode = (unsigned int)(uint16_t)vdi_pb_contrl(&pb, 0);
    int16_t sub = vdi_pb_contrl(&pb, 5); // only meaningful for the GDP opcode

    const VdiCall *call = find_call(opcode);
    const char *name = (opcode == VDI_GDP) ? vdi_gdp_name(sub) : (call ? call->name : "Unknown");

    log_write(LOG_API, "VDI %s (0x%02X) called", name, opcode);

    if (call && call->handler) {
        call->handler(&pb);
    } else {
        printf("[vdi] Unimplemented VDI call %s (opcode 0x%02X)\n", name, opcode);
        log_write(LOG_ERROR, "VDI %s (opcode 0x%02X) is unimplemented, nothing drawn", name, opcode);
    }

    log_write(LOG_API, "VDI %s (0x%02X) done", name, opcode);
}