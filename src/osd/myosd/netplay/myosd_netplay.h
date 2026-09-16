// license:BSD-3-Clause
// copyright-holders: David Valdeita (Seleuco)
/***************************************************************************

    myosd_netplay.h

    Rollback netplay bridge: save/restore MAME machine states and the
    per-item CRC desync detector, without depending on MAME core types
    directly in the netplay / input layers.  Implemented in
    myosd_netplay.cpp.

    Declared here in the same most-central-first order as the .cpp: the
    input_update() trunk, the savestate bridge, the control accessors,
    then the debug-only desync diagnostics.

***************************************************************************/

#ifndef myosd_netplay_bridge_h
#define myosd_netplay_bridge_h

#include <cstdint>
#include <cstddef>
#include "myosd_core.h"   /* myosd_input_state: plain C struct, no MAME/emu.h deps */

/* Forward declaration only (matches netplay.h's "typedef struct netplay
 * netplay_t"): avoids pulling netplay.h -> myosd.h ahead of emu.h in
 * translation units that rely on the emu.h-first include order. */
struct netplay;
typedef struct netplay netplay_t;

/* Shared input-state instance (defined in input.cpp).  myosd_netplay.cpp
 * reads/writes it from apply_netplay_input_state() to inject network input
 * into MAME and from my_osd_interface::input_update()'s netplay step. */
extern myosd_input_state g_input;

/* Netplay step of my_osd_interface::input_update(), called once per frame
 * while a netplay game is running (input.cpp).  Returns true if
 * input_update() must return immediately this call (a sync wait or a
 * fast-forward re-injection consumed the frame). */
bool myosd_netplay_input_update(netplay_t *handle, bool is_new_mame_frame, bool is_java_paused);

/* Ring buffer capture/restore, keyed by frame % ROLLBACK_RING_FRAMES. */
void myosd_netplay_state_capture(uint32_t frame);                  /* save the live machine into the slot */
void myosd_netplay_state_load(uint32_t frame);                     /* restore the slot into the live machine */
void myosd_netplay_state_inject(uint32_t frame, const uint8_t *buffer, uint32_t size); /* store peer buffer + load now (boot-time sync) */
void myosd_netplay_state_store(uint32_t frame, const uint8_t *buffer, uint32_t size);  /* store peer buffer, deferred load (mid-game resync) */
uint32_t myosd_netplay_state_checksum(uint32_t frame);              /* cached CRC of the slot, 0 if unavailable */
const uint8_t* myosd_netplay_get_state_buffer(uint32_t frame);      /* raw slot bytes; see NOTE in the .cpp on lock lifetime */
void myosd_netplay_state_cleanup();                                 /* free every ring slot (game exit) */
size_t myosd_netplay_get_state_size();                              /* byte size of one savestate (rollback size gate) */

/* Latch a savestate reload of ring slot `frame` to run at the next clean
 * scheduler boundary (see myosd_netplay_service_deferred_load).  Used for
 * both the mid-game RESYNC (netplay.cpp) and the legacy boot-time transfer. */
void myosd_netplay_rollback_arm_pending_load(uint32_t frame);

/* Cancel the rollback FF episode's rate-control drift and any pending deferred
 * load -- called when a mid-game RESYNC replaces the timeline wholesale.  (FF
 * suppression itself is cleared by the caller via set_ff_active(0).) */
void myosd_netplay_rollback_reset_for_resync(void);

/* Rollback fast-forward state, checked by myosd_droid.cpp's draw/audio callbacks. */
void myosd_netplay_set_ff_active(int active);   /* mark/clear a fast-forward replay burst */
int  myosd_netplay_get_ff_active();
void myosd_netplay_set_audio_mute(int mute);    /* mute/unmute audio during fast-forward */
int  myosd_netplay_get_audio_mute();

/* Apply a machine speed factor (per-mille, 1000 = 100%); used by the dynamic
 * rate controller in myosd_netplay_input_update() (see NETPLAY_RATE_* in
 * netplay.h). */
void myosd_netplay_set_speed(int speed);

/* Debug: arm/disarm the rollback determinism self-check (see state_capture). */
void myosd_netplay_set_selfcheck_probe(int on);
int  myosd_netplay_get_selfcheck_probe();

/* Desync detector: per-item / sectional CRC diagnostics (see netplay.cpp). */
void myosd_netplay_log_sectional_crc(uint32_t frame);               /* log a CRC per 1/8th of the state blob */
uint32_t myosd_netplay_get_item_crc_table(uint32_t frame, uint32_t *out, uint32_t max_items); /* fill our per-item CRC table */
void myosd_netplay_diff_item_crc_table(uint32_t frame, const uint32_t *peer, uint32_t peer_count); /* diff our slot vs peer's table */
void myosd_netplay_diff_item_crc_tables(uint32_t frame, const uint32_t *local, uint32_t local_count, const uint32_t *peer, uint32_t peer_count); /* diff two precomputed tables */
bool myosd_netplay_desync_tolerant(void);                                    /* running driver needs the broad-divergence rule (CPS-3) */
void myosd_netplay_section_fingerprints(uint32_t frame, uint8_t *out, int n); /* n 1-byte per-section RAM fingerprints of a slot */

#endif
