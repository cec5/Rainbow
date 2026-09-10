#include "aes/aes_appl.h"
#include "logger.h"

// [FULL] Always hands back ap_id 1, since the layer only ever runs one application.
void aes_appl_init(const AesPB *pb) {
    aes_pb_set_intout(pb, 0, 1);

    // Some binding libraries read the version, app count and app id from the global array rather than the return value.
    aes_pb_set_global(pb, 0, 0x0140);
    aes_pb_set_global(pb, 1, 1);
    aes_pb_set_global(pb, 2, 1);

    log_write(LOG_API, "appl_init -> ap_id=1, aes_version=0x0140");
}

// [NO-OP] Nothing is registered by appl_init(), so there is nothing to unregister; the guest halts through Pterm instead.
void aes_appl_exit(const AesPB *pb) {
    aes_pb_set_intout(pb, 0, 1);

    log_write(LOG_API, "appl_exit -> ok");
}