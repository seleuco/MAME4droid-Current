// license:BSD-3-Clause
// copyright-holders: David Valdeita (Seleuco)
/***************************************************************************

    myosd_netplay.cpp

    Rollback netplay bridge functions, plus the netplay step of
    my_osd_interface::input_update() (myosd_netplay_input_update and its
    MAME-dependent helpers): save/restore MAME machine states, write
    netplay_state_t into g_input, and drive the rollback fast-forward state
    machine.  Everything here either touches g_input/MAME types directly or
    wires the agnostic handle->rollback_* callbacks that let netplay.cpp
    stay free of them; protocol/session logic with no such dependency (the
    game-start bootstrap sequence) lives in netplay.cpp instead.

    Layout, most central first:
      1. Core shared state (rollback ring buffer, FF/audio/selfcheck flags)
      2. Netplay step of my_osd_interface::input_update() (the trunk):
         myosd_netplay_input_update() and its helpers
      3. Savestate capture/restore bridge (myosd_netplay_state_*)
      4. FF/audio/speed/selfcheck control accessors
      5. Desync detector / per-item CRC diagnostics (debug-only)
      6. Applied-input trace (debug-only, off by default)

***************************************************************************/

#include "myosd_netplay.h"

#include "emu.h"
#include "screen.h"
#include "myosd.h"

#include "save.h"      /* ram_state */
#include "myosd_slim_state.h" /* rollback capture minus immutable bulk regions */
#include "myosd_save_hacks.h" /* per-game rollback exclusion policy */
#include "netplay.h"   /* ROLLBACK_MAX_FRAMES */
#include "hashing.h"   /* util::crc32_creator */
#include "romload.h"   /* ROMENTRY_ISSYSTEM_BIOS / ROM_GETBIOSFLAGS (bios pin) */

#include <android/log.h>
#include <mutex>
#include <cstring>   // strncmp (gameplay-CRC item filter)
#include <cstdlib>   // malloc/free (initial-sync state buffers)
#include <cstdio>    // snprintf (netplay_warn toast formatting)
#include <chrono>    // capture-cost telemetry (TELEM perf)
#include <sys/time.h> // gettimeofday (wall-clock stamps, debounce timers)

#define NLOG(...) do { if(NETPLAY_LOG_ENABLED) __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay", __VA_ARGS__); } while(0)

/* Defined in myosdmain.cpp; this TU only reads it. */
extern my_osd_interface *osdInterface;

/* Droid bridge (myosd_droid.cpp) - netplay restart / pause helpers.  Plain C++
 * linkage, same as the declarations netplay.cpp uses for this family.        */
int  myosd_droid_netplay_restart_pending(void);
int  myosd_droid_netplay_game_settled(void);
void myosd_droid_netplay_set_exitPause(int val);
extern unsigned long myosd_droid_netplay_joystick_read(int i);

/* Local menu-combo interception (kept here, not in the agnostic netplay.cpp):
 * START+SELECT requests the MAME menu (one-shot flag for ui.cpp) and is STRIPPED
 * from the value netplay both applies locally AND sends -> symmetric, no desync;
 * the peer neither opens a menu nor credits a coin.  netplay.cpp calls this to
 * read the local player's digital input. */
static volatile int s_netplay_menu_request = 0;
uint32_t myosd_netplay_read_local_digital(void) {
    uint32_t d = (uint32_t)myosd_droid_netplay_joystick_read(0);
    const uint32_t MENU_COMBO = (1u << 8) | (1u << 9);   /* MYOSD_START | MYOSD_SELECT */
    if ((d & MENU_COMBO) == MENU_COMBO) {
        s_netplay_menu_request = 1;
        d &= ~MENU_COMBO;
    }
    return d;
}

/* One-shot test-and-clear consumed by ui.cpp handler_ingame to open the menu. */
int myosd_netplay_consume_menu_request(void) {
    int v = s_netplay_menu_request;
    s_netplay_menu_request = 0;
    return v;
}

/* ============================================================
 * SECTION 1 -- Core shared state
 * The rollback ring buffer and the flags read/written across every other
 * section below (the trunk, the savestate bridge, the control accessors
 * and the debug diagnostics all touch these).
 * ============================================================ */

/* Ring buffer: one slim_state per slot, indexed by (frame % ROLLBACK_RING_FRAMES).
 * slim_state == ram_state minus immutable bulk regions (see myosd_slim_state.h). */
static std::unique_ptr<myosd_slim_state> g_rb_states[ROLLBACK_MAX_FRAMES];
static uint32_t                   g_rb_frame_ids[ROLLBACK_MAX_FRAMES];
static uint32_t                   g_rb_crcs[ROLLBACK_MAX_FRAMES];
/* Per-slot FORWARD per-item CRC table, lock-step with g_rb_crcs.  Debug-only:
 * lets the RB_SELFCHECK probe name the exact save_items that first diverge. */
static std::vector<uint32_t>      g_rb_item_tables[ROLLBACK_MAX_FRAMES];
/* Per-slot FORWARD scheduler basetime at frame start, lock-step with
 * g_rb_crcs.  Debug-only: lets RB_SELFCHECK tell a bad frame length apart
 * from a bad saved state when comparing forward vs resim. */
static attotime                   g_rb_times[ROLLBACK_MAX_FRAMES];

/* Guards the ring buffer: written every frame by the GAME thread, read by
 * the NETWORK thread (desync detector / ITEM_DIFF probe).  ram_state::m_data
 * has no internal synchronization, so an unguarded cross-thread read would
 * race a same-slot save()/load().  Recursive: some entry points nest under
 * an already-locked caller on the same thread. */
static std::recursive_mutex       g_rb_ring_mutex;

/* Set while a rollback fast-forward pass is in progress (see the rollback
 * state machine below, myosd_netplay_input_update).  Checked by
 * droid_video_draw_cb (myosd_droid.cpp) to suppress rendering. */
static volatile int g_rb_ff_active = 0;

/* Set/cleared alongside g_rb_ff_active; checked by droid_sound_play_cb to
 * mute audio. */
static volatile int g_rb_audio_mute = 0;

/* Set around a forced null-rollback determinism probe (see
 * netplay_iu_rollback_normal_step below); gates the RB_SELFCHECK comparison
 * in myosd_netplay_state_capture (debug only).                              */
static volatile int g_rb_selfcheck_probe = 0;

/* ============================================================
 * SECTION 2 -- Netplay step of my_osd_interface::input_update() (the trunk)
 * myosd_netplay_input_update() is the single entry point called once per
 * frame from input.cpp while a netplay game is running; everything else in
 * this section is either state it depends on or a helper in its call chain.
 * ============================================================ */

/* Forward declarations: myosd_netplay_input_update() (right below) is
 * defined before the helpers it calls, so this section reads top-to-bottom
 * as entry point first, then its helpers in call order.  netplay_track_
 * connection / netplay_initial_sync / netplay_start_barrier are pure
 * protocol/session logic (no g_input, no MAME calls by name) and live in
 * netplay.cpp instead -- declared in netplay.h, included above. */
static void netplay_iu_on_game_start(netplay_t *handle, bool is_java_paused);
static bool netplay_iu_rollback_ff_step(netplay_t *handle, bool is_new_mame_frame);
static bool netplay_iu_rollback_normal_step(netplay_t *handle, bool is_new_mame_frame);
static void apply_netplay_input_state(bool is_new_mame_frame, int local_player, const netplay_state_t& local_state, int peer_player, const netplay_state_t& peer_state);
static inline void netplay_trace_applied(uint32_t frame, const netplay_state_t &loc, const netplay_state_t &peer, const char *src);
static void netplay_pin_neutral_input(netplay_t *h);

/* Rollback fast-forward state machine: when a prediction mismatch is
 * detected, myosd_netplay_input_update() loads the saved machine state for
 * the mispredicted frame and enters fast-forward mode.  For the next N calls
 * (one per MAME video frame) it injects historical corrected inputs until
 * the current frame is reached.  g_rb_ff_active itself lives in section 1,
 * shared with the video/audio suppression flag (myosd_netplay_set_ff_active). */
static uint32_t g_rb_ff_current = 0; /* next frame index to inject         */
static uint32_t g_rb_ff_target  = 0; /* first frame PAST the recovery range */
static uint32_t g_rb_ff_start   = 0; /* frame where the rollback began      */
static std::chrono::steady_clock::time_point
                g_rb_ff_t0;          /* episode start, for the completion TELEM */
static uint32_t g_rb_ff_armgen  = 0; /* handle->rollback_arm_gen latched when the
                                      * FF replays g_rb_ff_start; the FF-end clear
                                      * only fires if it is still unchanged.     */

/* Deferred rollback load: set when a rollback is detected inside
 * myosd_netplay_input_update() (a vblank timer callback).  The actual
 * state_load must run at a clean scheduler boundary, so we only latch the
 * target here and let running_machine::run() -> myosd_netplay_service_
 * deferred_load() do it. */
static volatile int g_rb_pending_load       = 0;
static uint32_t     g_rb_pending_load_frame = 0;

/* Armed by the primary screen's vblank-OFF callback and serviced at the next
 * clean boundary: 1 = a ring capture is due.  g_rb_boundary_skip_frame
 * suppresses the one capture right after a deferred load (that slot already
 * holds the canonical loaded state). */
static volatile int g_rb_capture_pending    = 0;
static uint32_t     g_rb_boundary_skip_frame = 0xffffffffu;

/* Drop-in handover: the ring capture skips frame 0, so netplay.cpp arms this to
 * make the next clean boundary capture the handover frame; boot_slot_clean
 * latches when done so it can serialize a slot that loads without a re-arm. */
static volatile int g_rb_boot_capture_pending = 0;
static volatile int g_rb_boot_slot_clean      = 0;

/* Duplicate-new-frame guard: a resync deferred load can fire frame_update twice
 * for the same video frame; keyed on the screen frame_number (not machine time,
 * which differs between the two fires) it swallows the spurious one so exactly
 * one netplay frame maps to each video frame.  Scoped to g_dup_window. */
static uint64_t g_last_new_frame_vframe = UINT64_MAX;
static int      g_dup_window            = 0;

/* Post-resync phase diagnostic (NLOG-gated): after a handover load applies, log
 * netplay frame vs screen video frame_number on both peers; if the roles map a
 * netplay frame to different video frames, the desync is a 1-frame phase slip. */
static int      g_resync_phase_trace   = 0;

static uint64_t netplay_primary_screen_frame(void)
{
    if (osdInterface == nullptr || !osdInterface->isMachine())
        return 0;
    screen_device *s = screen_device_enumerator(osdInterface->machine().root_device()).first();
    return s ? (uint64_t)s->frame_number() : 0;
}

/* Dynamic rate control state (see NETPLAY_RATE_* in netplay.h).  Lives on
 * the stall-loop exit path below; persists across frames.
 * g_rate_adv_ema_x16 is the frame advantage smoothed with a ~16-frame EMA,
 * stored x16 to keep integer precision. */
static int      g_rate_speed       = 1000; /* last speed factor applied      */
static long     g_rate_adv_ema_x16 = 0;
static uint32_t g_rate_last_eval   = 0;    /* frame of the last speed step   */

/* Netplay step of my_osd_interface::input_update() (declared in
 * myosd_netplay.h).  Returns true if input_update() must return
 * immediately this call. */
bool myosd_netplay_input_update(netplay_t *handle, bool is_new_mame_frame, bool is_java_paused)
{
    netplay_track_connection(handle);
    netplay_iu_on_game_start(handle, is_java_paused);

    if (netplay_initial_sync(handle)) {
        /* No frame input applies on this call, so g_input still holds the raw
         * local pad that MACHINE_NOTIFY_FRAME latches next -- and the field after
         * a handover load reads that latch.  Pin neutral, identical on both peers. */
        netplay_pin_neutral_input(handle);
        return true;
    }

    netplay_start_barrier(handle);

    bool is_locally_paused = (handle && handle->has_connection && handle->has_begun_game && myosd_is_paused());

    if (g_rb_ff_active && handle)
        NLOG("RB_IU reached hc=%d hbg=%d paused=%d is_new=%d mode=%d rb_en=%d ff_cur=%u",
             handle->has_connection, handle->has_begun_game, myosd_is_paused(),
             is_new_mame_frame, handle->mode, handle->rollback_enabled, g_rb_ff_current);

    if (is_locally_paused) {
        // MAME's game CPU will skip this frame.
        // Do NOT advance netplay frame counter. Send immediate heartbeat to peer.
        netplay_send_data(handle);

    } else if (handle && handle->has_connection && handle->has_begun_game &&
               handle->mode == NETPLAY_MODE_ROLLBACK && handle->rollback_enabled) {
        // ── ROLLBACK MODE ────────────────────────────────────────────────
        if (g_rb_ff_active) {
            if (netplay_iu_rollback_ff_step(handle, is_new_mame_frame))
                return true;
        }

        if (!g_rb_ff_active) {
            if (netplay_iu_rollback_normal_step(handle, is_new_mame_frame))
                return true;
        }
        // ── END ROLLBACK MODE ────────────────────────────────────────────

    } else {
        // ── LOCKSTEP MODE ────────────────────────────────────────────────
        // MAME will execute the game CPU for this frame!
        if (is_new_mame_frame) {
            // 1. Block and wait for peer's inputs for this frame.
            netplay_pre_frame_net(handle);

            // 2. Read local hardware joystick into buffer, advance netplay frame counter, and send to peer.
            netplay_post_frame_net(handle);
        }
    }

    /* Apply inputs for LOCKSTEP mode.  ROLLBACK mode calls
     * apply_netplay_input_state() internally in all its branches.     */
    if (handle && handle->has_connection && handle->has_begun_game && !is_locally_paused &&
        !(handle->mode == NETPLAY_MODE_ROLLBACK && handle->rollback_enabled)) {
        if (handle->player1) {
            // Host: Local input is P1, Peer input is P2
            apply_netplay_input_state(is_new_mame_frame, 0, handle->state, 1, handle->peer_state);
        } else {
            // Client: Peer input (Host) is P1, Local input is P2
            apply_netplay_input_state(is_new_mame_frame, 1, handle->state, 0, handle->peer_state);
        }
    }

    return false;
}

/* Clean-boundary hook (declared in netplay.h, called from machine.cpp run loop). */
extern "C" void myosd_netplay_service_deferred_load(void)
{
    if (g_rb_pending_load) {
        uint32_t f = g_rb_pending_load_frame;
        g_rb_pending_load = 0;
        myosd_netplay_state_load(f);
        /* Close the RESYNC episode HERE, the instant the synced state is APPLIED
         * -- not when it finished assembling.  resync_active poisons incoming DATA
         * (netplay.cpp); lowering it earlier let a late DATA for the OLD timeline
         * hit the not-yet-loaded state (the post-handover desync).  No-op for a
         * normal rollback load, which never sets resync_active. */
        {
            netplay_t *rh = netplay_get_handle();
            if (rh && rh->resync_active) {
                rh->resync_active = 0;
                /* Episode over: clear the boot-slot latch so the next drop-in
                 * re-captures a fresh handover slot instead of a stale one. */
                g_rb_boot_slot_clean = 0;
                struct timeval rtv;
                gettimeofday(&rtv, NULL);
                rh->resync_last_done_ms =
                    (uint32_t)((rtv.tv_sec * 1000) + (rtv.tv_usec / 1000));
                g_resync_phase_trace = 12;   /* arm the post-resync phase diagnostic */
                NLOG("RESYNC applied (deferred load) netplay_frame=%u video_frame=%llu role=%s",
                     f, (unsigned long long)netplay_primary_screen_frame(),
                     rh->player1 ? "HOST" : "CLIENT");
            }
        }
        /* Arm the duplicate-new-frame guard for a resync load only: a normal
         * rollback (g_rb_ff_active) has the FF loop's own counting.  The host
         * can fire frame_update twice for the same video frame after the load;
         * the guard swallows the spurious one so the peers stay aligned. */
        if (!g_rb_ff_active) {
            g_last_new_frame_vframe = UINT64_MAX;
            g_dup_window = 2;
        }
        /* Every slot is captured at the clean vblank tail with the vblank timer
         * already armed, so it fires naturally on load (no screen re-arm).  Just
         * suppress the one ring re-capture right after this load: the slot is
         * already the canonical loaded state. */
        g_rb_boundary_skip_frame = f;
        NLOG("RB_DEFER load done frame=%u ff_active=%d ff_cur=%u ff_tgt=%u",
             f, g_rb_ff_active, g_rb_ff_current, g_rb_ff_target);
    }

    /* Clean-boundary capture: serviced only where the screen's vblank-OFF
     * callback armed g_rb_capture_pending, so the slot lands at the frame tail
     * (IRQ queue drained, vblank timer armed).  A slot taken at an arbitrary
     * boundary reloads with no armed vblank timer and starves -- so ring and
     * drop-in handover slots must both be taken here. */
    if (g_rb_capture_pending) {
        g_rb_capture_pending = 0;
        netplay_t *ch = netplay_get_handle();
        if (ch && ch->has_begun_game &&
            ch->mode == NETPLAY_MODE_ROLLBACK && ch->rollback_enabled &&
            ch->rollback_capture_state) {
            if (g_rb_boot_capture_pending) {
                /* Drop-in / boot handover slot: the ring path below skips frame
                 * 0 and lags the first frames, so the handover frame is captured
                 * here at the same vblank tail phase and loads with no re-arm.
                 * initial_sync freezes handle->frame while it waits, so ch->frame
                 * is the exact frame being handed over. */
                ch->rollback_capture_state(ch->frame);
                g_rb_boot_capture_pending = 0;
                g_rb_boot_slot_clean      = 1;
            } else {
                /* Slot = the frame about to run (its pre-execution tail state).
                 * In fast-forward ch->frame lags, so use g_rb_ff_current or the
                 * slot lands one frame short and corrupts the ring. */
                uint32_t cf = g_rb_ff_active ? g_rb_ff_current : ch->frame;

                /* DEV (off): a real desync to calibrate the CPS-3 rule -- the host
                 * quietly reloads a slot SIM_DESYNC_BACK frames old, once, with
                 * nothing older still rollbackable so it can't heal. */
                constexpr bool     NETPLAY_SIM_DESYNC_ENABLED = false;
                constexpr uint32_t SIM_DESYNC_AT   = 1800;   /* ~30s into the session */
                constexpr uint32_t SIM_DESYNC_BACK = 60;
                if (NETPLAY_SIM_DESYNC_ENABLED && ch->player1) {
                    static bool s_sim_done = false;
                    if (cf < SIM_DESYNC_AT) s_sim_done = false;   /* new session */
                    else if (!s_sim_done && !g_rb_ff_active && !g_rb_pending_load &&
                             !ch->resync_active && ch->confirmed_watermark + 1 >= cf) {
                        s_sim_done = true;
                        myosd_netplay_state_load(cf - SIM_DESYNC_BACK);
                        NLOG("SIM_DESYNC host loaded slot %u at frame %u (real desync from here)",
                             cf - SIM_DESYNC_BACK, cf);
                    }
                }

                if (cf != 0 && cf != g_rb_boundary_skip_frame)
                    ch->rollback_capture_state(cf);
            }
            g_rb_boundary_skip_frame = 0xffffffffu;
        }
    }
}

/* vblank-OFF arm hook for the clean-boundary ring capture (declared in
 * netplay.h).  Called from screen_device::vblank_end (primary screen) once per
 * frame so the capture lands at the frame tail instead of mid-vblank_begin. */
extern "C" void myosd_netplay_arm_boundary_capture(void)
{
    g_rb_capture_pending = 1;
}

/* Drop-in handover clean capture (declared in netplay.h).  netplay_initial_sync
 * arms it and then waits (returning true from input_update each frame) until
 * _ready latches, so the handover frame is a coherent vblank-tail slot that
 * loads with no screen re-arm -- see the boot capture in service_deferred_load. */
extern "C" void myosd_netplay_arm_boot_slot_capture(void)
{
    g_rb_boot_slot_clean      = 0;
    g_rb_boot_capture_pending = 1;
}

extern "C" int myosd_netplay_boot_slot_ready(void)
{
    return g_rb_boot_slot_clean;
}

/* Latch a deferred reload; see myosd_netplay.h and myosd_netplay_service_
 * deferred_load above.  Exposed so netplay.cpp's session/sync logic can
 * schedule one without touching the (private) pending-load state itself. */
void myosd_netplay_rollback_arm_pending_load(uint32_t frame)
{
    g_rb_pending_load       = 1;
    g_rb_pending_load_frame = frame;
}

/* Cancel any pending deferred load and reset the dynamic rate controller to
 * 1000 (100%); see myosd_netplay.h.  Called at the start of a mid-game
 * RESYNC episode, which replaces the timeline wholesale. */
void myosd_netplay_rollback_reset_for_resync(void)
{
    g_rb_pending_load = 0;      /* superseded; re-latched by the caller   */
    if (g_rate_speed != 1000) {
        g_rate_speed = 1000;
        myosd_netplay_set_speed(1000);
    }
    g_rate_adv_ema_x16 = 0;
}

/* Per-player last-seen absolute mouse position (anchor) and the accumulated
 * delta since the current MAME frame started (pending), used to turn the
 * netplay state's absolute mouse coordinates into relative motion. */
static float netplay_anchor_mouse_x[2] = {0, 0};
static float netplay_anchor_mouse_y[2] = {0, 0};
static float netplay_pending_mouse_x[2] = {0, 0};
static float netplay_pending_mouse_y[2] = {0, 0};

/* Push one local/peer netplay_state_t pair into g_input for this frame:
 * digital, analog, mouse (anchor-delta) and lightgun axes for both players. */
static void apply_netplay_input_state(bool is_new_mame_frame, int local_player, const netplay_state_t& local_state, int peer_player, const netplay_state_t& peer_state) {
    g_input.joy_status[local_player] = local_state.digital;
    g_input.joy_status[peer_player]  = peer_state.digital;

    g_input.joy_analog[local_player][MYOSD_AXIS_LX] = local_state.analog_x;
    g_input.joy_analog[local_player][MYOSD_AXIS_LY] = local_state.analog_y;
    g_input.joy_analog[local_player][MYOSD_AXIS_RX] = local_state.analog_rx;
    g_input.joy_analog[local_player][MYOSD_AXIS_RY] = local_state.analog_ry;
    g_input.joy_analog[local_player][MYOSD_AXIS_LZ] = local_state.analog_lz;
    g_input.joy_analog[local_player][MYOSD_AXIS_RZ] = local_state.analog_rz;

    g_input.joy_analog[peer_player][MYOSD_AXIS_LX] = peer_state.analog_x;
    g_input.joy_analog[peer_player][MYOSD_AXIS_LY] = peer_state.analog_y;
    g_input.joy_analog[peer_player][MYOSD_AXIS_RX] = peer_state.analog_rx;
    g_input.joy_analog[peer_player][MYOSD_AXIS_RY] = peer_state.analog_ry;
    g_input.joy_analog[peer_player][MYOSD_AXIS_LZ] = peer_state.analog_lz;
    g_input.joy_analog[peer_player][MYOSD_AXIS_RZ] = peer_state.analog_rz;

    g_input.mouse_status[local_player] = local_state.mouse_status;
    g_input.mouse_status[peer_player]  = peer_state.mouse_status;

    if (is_new_mame_frame) {
        netplay_pending_mouse_x[local_player] = 0;
        netplay_pending_mouse_y[local_player] = 0;
        netplay_pending_mouse_x[peer_player] = 0;
        netplay_pending_mouse_y[peer_player] = 0;
    }

    float local_dx = local_state.mouse_x - netplay_anchor_mouse_x[local_player];
    float local_dy = local_state.mouse_y - netplay_anchor_mouse_y[local_player];
    netplay_anchor_mouse_x[local_player] = local_state.mouse_x;
    netplay_anchor_mouse_y[local_player] = local_state.mouse_y;

    netplay_pending_mouse_x[local_player] += local_dx;
    netplay_pending_mouse_y[local_player] += local_dy;

    g_input.mouse_x[local_player] = netplay_pending_mouse_x[local_player];
    g_input.mouse_y[local_player] = netplay_pending_mouse_y[local_player];

    float peer_dx = peer_state.mouse_x - netplay_anchor_mouse_x[peer_player];
    float peer_dy = peer_state.mouse_y - netplay_anchor_mouse_y[peer_player];
    netplay_anchor_mouse_x[peer_player] = peer_state.mouse_x;
    netplay_anchor_mouse_y[peer_player] = peer_state.mouse_y;

    netplay_pending_mouse_x[peer_player] += peer_dx;
    netplay_pending_mouse_y[peer_player] += peer_dy;

    g_input.mouse_x[peer_player] = netplay_pending_mouse_x[peer_player];
    g_input.mouse_y[peer_player] = netplay_pending_mouse_y[peer_player];

    g_input.lightgun_x[local_player] = local_state.lightgun_x;
    g_input.lightgun_y[local_player] = local_state.lightgun_y;
    g_input.lightgun_x[peer_player]  = peer_state.lightgun_x;
    g_input.lightgun_y[peer_player]  = peer_state.lightgun_y;
}

/* Neutral input, identical on both peers, for calls where no frame input is
 * applied (initial sync / handover).  ioport latches g_input at NOTIFY_FRAME and
 * the field after a load reads that latch, so the raw local pad must not reach
 * it.  Mouse accumulators and anchors reset too, so the anchor-delta agrees. */
static void netplay_pin_neutral_input(netplay_t *h)
{
    static const netplay_state_t neutral = {};
    int lp = h->player1 ? 0 : 1, pp = h->player1 ? 1 : 0;
    apply_netplay_input_state(false, lp, neutral, pp, neutral);
    for (int p = 0; p < 2; p++) {
        netplay_pending_mouse_x[p] = netplay_pending_mouse_y[p] = 0;
        g_input.mouse_x[p] = g_input.mouse_y[p] = 0;
    }
}

/* Can this game hand over to a drop-in joiner?  The handover IS the rollback
 * state transfer, so a savestate too big for the ring leaves the joiner at
 * frame zero against a host minutes in.  Measured once per game. */
static void netplay_drop_in_probe(netplay_t *handle)
{
    size_t state_sz = myosd_netplay_get_state_size();

    /* Machine not up yet: leave it unmeasured and ask again next frame
     * rather than latch a guess. */
    if (state_sz == 0)
        return;

    uint32_t ring = ROLLBACK_MAX_FRAMES;
    uint64_t by_budget = (uint64_t)ROLLBACK_RING_RAM_BUDGET / (uint64_t)state_sz;
    if (by_budget < ring) ring = (uint32_t)by_budget;

    handle->drop_in_state_kb = (int)(state_sz / 1024);

    if (state_sz > ROLLBACK_STATE_SIZE_LIMIT || ring < ROLLBACK_MIN_FRAMES) {
        /* No warning from here: this is a verdict to be read, not a
         * fallback that has already happened. */
        handle->drop_in_state = 2;
        NLOG("DROP-IN unavailable: state %zu bytes (limit %d, ring %u frames)",
             state_sz, ROLLBACK_STATE_SIZE_LIMIT, ring);
    } else {
        handle->drop_in_state = 1;
        NLOG("DROP-IN available: state %zu bytes, ring %u frames", state_sz, ring);
    }
}

/* Netplay must not "begin" on a machine the autostart didn't launch:
 * has_connection is set at socket creation (before JOIN), so connecting inside a
 * running ROM would freeze it on the sync barriers.  Drop-in holds off too --
 * nobody joined yet, so has_begun_game stays 0 and the game keeps the single-
 * player path (every netplay hook is gated on it). */
static void netplay_iu_on_game_start(netplay_t *handle, bool is_java_paused)
{
    /* Outside the guard below: restart_pending() stays 1 while nobody has
     * joined, which is the whole life of a drop-in host waiting alone.
     * Read-only -- summing the save entries touches nothing rollback owns. */
    if (handle && handle->drop_in && handle->player1 && handle->drop_in_state == 0 &&
        handle->has_connection && !handle->has_joined &&
        myosd_droid_netplay_game_settled())
        netplay_drop_in_probe(handle);

    if (!(handle && handle->has_connection && !handle->has_begun_game && !is_java_paused &&
          !myosd_droid_netplay_restart_pending()))
        return;

    if (handle->drop_in && !handle->has_joined)
        return;

    handle->has_begun_game = 1;
    handle->peer_state_size = 0;   /* fresh per game for the size probe */
    /* Telemetry: wall-clock of the very first gameplay frame, per
     * role, to compare HOST vs CLIENT start-up asymmetry. */
    {
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        NLOG("TELEM has_begun_game role=%s wallclock_ms=%lld mode=%d",
             handle->player1 ? "HOST" : "CLIENT", (long long)now_ms, handle->mode);
    }
    netplay_anchor_mouse_x[0] = 0;
    netplay_anchor_mouse_y[0] = 0;
    netplay_anchor_mouse_x[1] = 0;
    netplay_anchor_mouse_y[1] = 0;
    /* Fresh rollback bookkeeping for the new game: clear the drop-in boot-slot
     * latches (a stale =1 from a prior session skips the boundary capture and
     * drops to the mid-vblank inline fallback that freezes the joiner) and drop
     * the per-entry slim classification cache so it rebuilds for this driver. */
    g_rb_boot_slot_clean      = 0;
    g_rb_boot_capture_pending = 0;
    myosd_slim_state::invalidate_class_cache();
    __sync_synchronize();

    /* ── Rollback setup ────────────────────────────────────────── *
     * Wire the bridge callbacks so that netplay_pre_frame_net can  *
     * save machine states without depending on MAME core types.    */
    handle->rollback_capture_state    = myosd_netplay_state_capture;
    handle->rollback_load_state       = myosd_netplay_state_load;
    handle->rollback_cleanup_states   = myosd_netplay_state_cleanup;
    handle->rollback_capture_checksum = myosd_netplay_state_checksum;
    handle->rollback_query_checksum   = myosd_netplay_state_checksum;
    handle->rollback_inject_state     = myosd_netplay_state_inject;
    handle->rollback_store_state      = myosd_netplay_state_store;
    handle->rollback_get_state_buffer = myosd_netplay_get_state_buffer;
    handle->rollback_get_state_size   = myosd_netplay_get_state_size;

    /* Check the state size once at game-start.  The ring depth is
     * ADAPTIVE: ring = min(ROLLBACK_MAX_FRAMES, BUDGET/state_sz).  If
     * that yields fewer than ROLLBACK_MIN_FRAMES slots, fall back to
     * LOCKSTEP like the hard size limit; both peers compute this
     * identically (state_sz/BUDGET are symmetric). */
    if (handle->mode == NETPLAY_MODE_ROLLBACK) {
        size_t state_sz = myosd_netplay_get_state_size();
        uint32_t ring = ROLLBACK_MAX_FRAMES;
        if (state_sz > 0) {
            uint64_t by_budget = (uint64_t)ROLLBACK_RING_RAM_BUDGET / (uint64_t)state_sz;
            if (by_budget < ring)
                ring = (uint32_t)by_budget;
        }
        if (state_sz > ROLLBACK_STATE_SIZE_LIMIT || ring < ROLLBACK_MIN_FRAMES) {
            handle->rollback_enabled = 0;
            /* Big-state fallback: switch MODE to LOCKSTEP rather than
             * leaving rollback_enabled=0 with mode still ROLLBACK
             * (that combo made pre/post_frame_net both no-ops).  Both
             * peers compute state_sz symmetrically, so lockstep's
             * pristine CHD/NVRAM boot needs no state transfer. */
            handle->mode = NETPLAY_MODE_LOCKSTEP;
            handle->initial_sync_complete = 1;
            NLOG("ROLLBACK disabled: state %zu bytes (limit %d) would give a %u-frame ring (min %d, budget %u MB) - falling back to LOCKSTEP",
                 state_sz, ROLLBACK_STATE_SIZE_LIMIT, ring, ROLLBACK_MIN_FRAMES,
                 (unsigned)(ROLLBACK_RING_RAM_BUDGET / (1024u * 1024u)));
            if (handle->netplay_warn) {
                char warn_buf[64];
                snprintf(warn_buf, sizeof(warn_buf),
                         "TOAST:@state_too_large|%.2f",
                         (double)state_sz / (1024.0 * 1024.0));
                handle->netplay_warn(warn_buf);
            }
        } else {
            handle->rollback_enabled = 1;
            myosd_netplay_ring_frames = ring;
            NLOG("ROLLBACK enabled: savestate size = %zu bytes, ring = %u frames (%.1f MB)",
                 state_sz, ring, (double)state_sz * ring / (1024.0 * 1024.0));
        }
        /* Reset fast-forward machine state for this new game.     */
        g_rb_ff_current = 0;
        g_rb_ff_target  = 0;
        g_dup_window    = 0;                        /* BE.2: guard inert until a resync */
        myosd_netplay_set_ff_active(0);
        myosd_netplay_set_audio_mute(0);
    }
}

/* Fast-forward replay state machine: while g_rb_ff_active, re-simulates
 * frames g_rb_ff_start..g_rb_ff_target after a rollback correction.
 * Returns true if myosd_netplay_input_update() must return immediately
 * this call. */
static bool netplay_iu_rollback_ff_step(netplay_t *handle, bool is_new_mame_frame)
{
    NLOG("RB_FF entry cur=%u tgt=%u start=%u new=%d",
         g_rb_ff_current, g_rb_ff_target, g_rb_ff_start, is_new_mame_frame);
    /* MAME calls input_update twice per frame (true then false).
     * On the second call the FF state machine must NOT advance,
     * since the CPU hasn't simulated the frame yet; re-inject the
     * historical inputs, since input_poll just overwrote g_input
     * with the physical joystick. */
    if (!is_new_mame_frame) {
        /* g_rb_ff_current was advanced during the 'true' call, so
         * the frame currently being simulated is g_rb_ff_current-1. */
        uint32_t sim_frame = g_rb_ff_current - 1;
        int idx = (int)(sim_frame % ROLLBACK_RING_FRAMES);
        netplay_frame_history_t *h = &handle->frame_history[idx];
        /* Inject applied_peer_state, NOT peer_state: the network
         * thread can mutate peer_state between the two passes,
         * and the misprediction check compares against
         * applied_peer_state -- reading peer_state here would let
         * a divergence survive every rollback undetected. */
        if (handle->player1)
            apply_netplay_input_state(false, 0, h->local_state, 1, h->applied_peer_state);
        else
            apply_netplay_input_state(false, 1, h->local_state, 0, h->applied_peer_state);
        return true;
    }

    /* Fast-forward recovery: advance frame counter manually and
     * capture state. */
    if (g_rb_ff_current >= g_rb_ff_target) {
        /* Reached the target frame: end fast-forward. */
        myosd_netplay_set_ff_active(0);
        myosd_netplay_set_audio_mute(0);
        myosd_netplay_set_selfcheck_probe(0); /* end determinism probe (if any) */

        /* Set handle->frame so the normal block can capture it correctly. */
        pthread_mutex_lock(&handle->sync_mutex);
        handle->frame = g_rb_ff_current;

        /* Only clear requires_rollback for the EXACT frame that
         * triggered this FF (g_rb_ff_start) AND if no new arm
         * happened since (arm_gen unchanged) -- otherwise a later
         * frame or a mid-FF confirm-fix heal is not guaranteed
         * fixed, and clearing it wrongly would permanently desync. */
        if (handle->requires_rollback &&
            handle->rollback_to_frame == g_rb_ff_start &&
            handle->rollback_arm_gen == g_rb_ff_armgen) {
            handle->requires_rollback = 0;
        }
        /* The FF re-captured every frame it replayed, so the ring
         * is healed -- re-enable the CRC desync detector, unless a
         * NEW arm landed during the FF (requires_rollback still
         * set), in which case stay muted until that completes too. */
        if (!handle->requires_rollback)
            handle->crc_dirty = 0;
        pthread_mutex_unlock(&handle->sync_mutex);

        /* Depth + wall time of the whole resim burst: this runs
         * INSIDE one video frame's budget, so ms here is exactly
         * the hiccup the user feels. */
        NLOG("TELEM rb_ff depth=%u ms=%u frame=%u",
             g_rb_ff_target - g_rb_ff_start,
             (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - g_rb_ff_t0).count(),
             g_rb_ff_current);
        NLOG("ROLLBACK: fast-forward complete at frame %u", g_rb_ff_target);
        /* Fall through: the caller re-checks g_rb_ff_active (now 0) and
         * runs the normal rollback step for this same call. */
        return false;
    }

    /* About to simulate g_rb_ff_current: capture the state
     * BEFORE it executes, unless this is the start frame
     * (rb_frame) -- it was just loaded by state_load() and
     * already canonically captured, so capturing again would
     * trigger a redundant load() that can cause desyncs. */
    if (is_new_mame_frame) {
        /* The vblank-OFF arm captures this FF frame's slot at the tail (clean
         * boundary), so no mid-vblank capture here -- see section 3. */
        pthread_mutex_lock(&handle->sync_mutex);
        handle->frame = g_rb_ff_current;
        pthread_mutex_unlock(&handle->sync_mutex);
        myosd_netplay_set_ff_active(1); /* MAME's UI toggles this off each frame otherwise */
    }

    /* Inject inputs for g_rb_ff_current */
    int idx = (int)(g_rb_ff_current % ROLLBACK_RING_FRAMES);
    netplay_frame_history_t *h = &handle->frame_history[idx];

    /* Snapshot and update safely under mutex. */
    netplay_state_t snap_peer;
    netplay_state_t snap_local;

    if (is_new_mame_frame) {
        pthread_mutex_lock(&handle->sync_mutex);
        /* Latch the arm generation at the exact moment the
         * START frame's inputs are read (same critical section).
         * Any arm that lands after this point is NOT incorporated
         * in this FF's replay of the start frame, so the FF-end
         * clear must not fire (see completion block above).       */
        if (g_rb_ff_current == g_rb_ff_start)
            g_rb_ff_armgen = handle->rollback_arm_gen;
        /* A confirmation that arrived while this frame sat ahead of the rewound
         * handle->frame went to the early buffer, which only pre_frame_net drains:
         * adopt it here, or the frame stays predicted (uncorrected) and the
         * confirmed watermark -- and with it the CRC detector -- stalls for good. */
        int e_idx = (int)(g_rb_ff_current % EARLY_BUFFER_SIZE);
        if (handle->early_peer_frame[e_idx] == g_rb_ff_current) {
            h->peer_state     = handle->early_peer_state[e_idx];
            h->peer_confirmed = 1;
            handle->early_peer_frame[e_idx] = 0xFFFFFFFF;
        }
        h->applied_peer_state = h->peer_state;
        snap_peer = h->peer_state;
        snap_local = h->local_state;
        pthread_mutex_unlock(&handle->sync_mutex);
    } else {
        /* On non-new frame (poll retry), just reuse without mutex, as network
         * shouldn't corrupt the re-injection of what we already decided to apply. */
        snap_peer = h->applied_peer_state;
        snap_local = h->local_state;
    }

    if (handle->player1)
        apply_netplay_input_state(is_new_mame_frame, 0, snap_local, 1, snap_peer);
    else
        apply_netplay_input_state(is_new_mame_frame, 1, snap_local, 0, snap_peer);

    netplay_trace_applied(g_rb_ff_current, snap_local, snap_peer, "FF");

    if (is_new_mame_frame) {
        g_rb_ff_current++;
    }
    return true; /* Return immediately to let MAME execute this frame! */
}

/* Normal (non fast-forward) rollback step: decide between a fresh
 * rollback trigger, peer-pause wait, frame-advantage pacing, and the
 * ordinary forward frame.  Returns true if myosd_netplay_input_update()
 * must return immediately this call. */
static bool netplay_iu_rollback_normal_step(netplay_t *handle, bool is_new_mame_frame)
{
    /* Only process rollback/normal logic on the first input_update
     * call (is_new_mame_frame=true). The second call must re-inject
     * the same inputs to prevent overwriting. */
    if (!is_new_mame_frame) {
        /* Re-inject whatever was set by the first call. */
        if (handle->player1)
            apply_netplay_input_state(false, 0, handle->state, 1, handle->peer_state);
        else
            apply_netplay_input_state(false, 1, handle->state, 0, handle->peer_state);
        return true;
    }

    /* Post-resync window: the handover load can fire frame_update twice for the
     * same video frame; swallow the spurious second one (screen frame_number
     * unchanged) so the netplay counter waits for the next real video frame. */
    if (g_dup_window > 0) {
        g_dup_window--;
        uint64_t vnow = netplay_primary_screen_frame();
        if (vnow == g_last_new_frame_vframe) {
            NLOG("RESYNC: duplicate new-frame at same video frame swallowed (vframe=%llu netplay_frame=%u)",
                 (unsigned long long)vnow, handle->frame);
            if (handle->player1)
                apply_netplay_input_state(false, 0, handle->state, 1, handle->peer_state);
            else
                apply_netplay_input_state(false, 1, handle->state, 0, handle->peer_state);
            return true;
        }
        g_last_new_frame_vframe = vnow;
    }

    if (handle->requires_rollback) {
        /* Prediction mismatch: trigger rollback. */
        pthread_mutex_lock(&handle->sync_mutex);
        uint32_t rb_frame  = handle->rollback_to_frame;
        uint32_t cur_frame = handle->frame;
        handle->requires_rollback = 0;
        /* Baseline; re-latched when the FF replays rb_frame. */
        g_rb_ff_armgen = handle->rollback_arm_gen;
        pthread_mutex_unlock(&handle->sync_mutex);

        uint32_t depth = (cur_frame > rb_frame) ? (cur_frame - rb_frame) : 0;
        /* The maximum safely restorable depth is the (adaptive
         * effective) ring depth - 1: if depth == ring depth, the
         * current frame has ALREADY overwritten the target
         * frame's slot in the ring buffer. */
        uint32_t safe_depth = ROLLBACK_RING_FRAMES - 1 - handle->frame_skip;

        if (depth == 0) {
            /* Mismatch for a frame we haven't reached yet: it's
             * already in the early buffer/history, so just let
             * the game run normally -- the correction applies
             * when we naturally reach that frame. */
            NLOG("ROLLBACK: rb_frame=%u >= cur_frame=%u, skipping (handled by early buffer)", rb_frame, cur_frame);
            /* Nothing at/after rb_frame has executed yet, so no
             * stale captures exist -- unmute the CRC detector
             * (unless the net thread re-armed in the meantime). */
            pthread_mutex_lock(&handle->sync_mutex);
            if (!handle->requires_rollback)
                handle->crc_dirty = 0;
            pthread_mutex_unlock(&handle->sync_mutex);
            netplay_pre_frame_net(handle);
            netplay_post_frame_net(handle);
            if (handle->player1)
                apply_netplay_input_state(is_new_mame_frame, 0, handle->state, 1, handle->peer_state);
            else
                apply_netplay_input_state(is_new_mame_frame, 1, handle->state, 0, handle->peer_state);
            netplay_trace_applied(handle->frame - 1, handle->state, handle->peer_state, "FWD");
        } else if (depth <= safe_depth) {
            NLOG("ROLLBACK: rewind %u frames (%u to %u) [deferred load]", depth, cur_frame, rb_frame);

            /* Counted here and nowhere else: this is the branch that actually
             * re-simulates, so depth==0 and the too-deep case below stay out
             * of an average meant to say what a miss costs. */
            handle->tel_rollbacks++;
            handle->tel_rollback_frames += depth;

            /* N-1 recovery data is not tracked here: the ACK sends the
             * last known correct input instead. */

            myosd_netplay_set_audio_mute(1);
            myosd_netplay_set_ff_active(1);
            g_rb_ff_t0      = std::chrono::steady_clock::now();
            g_rb_ff_start   = rb_frame;
            /* Start the FF replay AT rb_frame: the FF loop itself
             * re-injects and executes rb_frame (its capture-skip
             * guard fires because current==start), then
             * rb_frame+1..cur, once the deferred load lands. */
            g_rb_ff_current = rb_frame;
            g_rb_ff_target  = cur_frame;
            pthread_mutex_lock(&handle->sync_mutex);
            handle->frame        = rb_frame;
            pthread_mutex_unlock(&handle->sync_mutex);

            /* Do NOT load here: input_update is dispatched from a
             * vblank timer callback, and loading there rebuilds
             * the timer list underneath execute_timers() so it
             * doesn't stay rewound.  Latch the target; the run
             * loop loads it at a clean boundary. */
            g_rb_pending_load       = 1;
            g_rb_pending_load_frame = rb_frame;
            return true;
        } else {
            /* Rollback depth out of range: the target slot was
             * already overwritten in the ring buffer, so run the
             * frame normally (the divergence is NOT corrected
             * here).  Logged to measure how often jitter pushes
             * rollbacks past the window in the field. */
            NLOG("TELEM rollback_overflow depth=%u safe_depth=%u rb_frame=%u cur=%u fs=%u rtt=%u",
                 depth, safe_depth, rb_frame, cur_frame, handle->frame_skip, handle->smoothed_rtt);
            /* The divergence here is REAL and uncorrectable --
             * unmute the CRC detector so it reports it instead of
             * masking it behind the dirty flag. */
            pthread_mutex_lock(&handle->sync_mutex);
            if (!handle->requires_rollback)
                handle->crc_dirty = 0;
            pthread_mutex_unlock(&handle->sync_mutex);
            netplay_pre_frame_net(handle);
            netplay_post_frame_net(handle);
            if (handle->player1)
                apply_netplay_input_state(is_new_mame_frame, 0, handle->state, 1, handle->peer_state);
            else
                apply_netplay_input_state(is_new_mame_frame, 1, handle->state, 0, handle->peer_state);
            netplay_trace_applied(handle->frame - 1, handle->state, handle->peer_state, "OVF");
        }
    } else {
        /* Peer-paused wait (rollback): rollback never blocks on the peer, so
         * without this we'd free-run against a frozen opponent; 10s of silence =
         * hangup.  Debounced (wall-clock) so a 1-frame blip doesn't flash the
         * "Peer is paused" toast. */
        static const uint32_t PEER_PAUSE_DEBOUNCE_MS = 130;
        static uint32_t s_peer_pause_since_ms = 0;
        uint32_t pp_now_ms;
        {
            struct timeval pptv; gettimeofday(&pptv, NULL);
            pp_now_ms = (uint32_t)((pptv.tv_sec * 1000) + (pptv.tv_usec / 1000));
        }
        bool peer_paused_now = (handle->is_peer_paused && !myosd_is_paused());
        if (peer_paused_now) {
            if (s_peer_pause_since_ms == 0) s_peer_pause_since_ms = pp_now_ms;
        } else {
            s_peer_pause_since_ms = 0;
        }
        bool peer_pause_confirmed = peer_paused_now &&
            (pp_now_ms - s_peer_pause_since_ms) >= PEER_PAUSE_DEBOUNCE_MS;

        /* Live debounced re-check for the advantage stall below: raw pause
         * blips (spurious during long boots) must NOT lift the stall -- that
         * let the fast peer free-run past the rollback window during boot
         * and desync permanently (mslug2 @70% field case). */
        auto peer_pause_confirmed_live = [&]() -> bool {
            struct timeval tv2; gettimeofday(&tv2, NULL);
            uint32_t now2 = (uint32_t)((tv2.tv_sec * 1000) + (tv2.tv_usec / 1000));
            bool pn = (handle->is_peer_paused && !myosd_is_paused());
            if (pn) { if (s_peer_pause_since_ms == 0) s_peer_pause_since_ms = now2; }
            else s_peer_pause_since_ms = 0;
            return pn && (now2 - s_peer_pause_since_ms) >= PEER_PAUSE_DEBOUNCE_MS;
        };

        if (handle->has_received_data && handle->has_connection &&
            peer_pause_confirmed && !handle->resync_active) {
            if (handle->netplay_warn)
                handle->netplay_warn((char*)"TOAST:@peer_paused");
            myosd_droid_netplay_set_exitPause(1);
            /* Park the rate controller: no live pacing while frozen. */
            if (g_rate_speed != 1000) {
                g_rate_speed = 1000;
                myosd_netplay_set_speed(1000);
            }
            g_rate_adv_ema_x16 = 0;
            auto pause_wait_t0 = std::chrono::steady_clock::now();
            NLOG("TELEM peer_pause_wait_begin frame=%u role=%s",
                 handle->frame, handle->player1 ? "HOST" : "CLIENT");
            /* A RESYNC request breaks the wait -- the peer pressing
             * Resync while paused is expected (the netplay dialog
             * pauses the emulation), and the sync block above must
             * engage on the next call. */
            while (handle->has_connection && handle->is_peer_paused &&
                   !myosd_is_paused() && !handle->resync_active) {
                netplay_send_data(handle);  /* heartbeat (carries our own pause flag) */
                struct timespec req = {0, 16000000}; // 16ms
                nanosleep(&req, NULL);
                /* Same wall clock as netplay.cpp's netplay_get_ticks_ms
                 * so the comparison with last_recv_time_ms is valid.  */
                struct timeval tv;
                gettimeofday(&tv, NULL);
                uint32_t now_ms = (uint32_t)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
                if (now_ms - handle->last_recv_time_ms >= 10000) {
                    NLOG("TELEM peer_pause_wait_hangup frame=%u (10s network silence)",
                         handle->frame);
                    if (handle->has_connection) {
                        handle->has_connection = 0;
                        netplay_warn_hangup(handle);
                    }
                    break;
                }
            }
            NLOG("TELEM peer_pause_wait_end frame=%u waited_ms=%lld connected=%d peer_paused=%d",
                 handle->frame,
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - pause_wait_t0).count(),
                 handle->has_connection, handle->is_peer_paused);
        }

        /* Frame-advantage time-sync (rollback drift fix): rollback
         * never blocks, so stall whenever more than
         * ROLLBACK_MAX_FRAME_ADVANTAGE frames ahead of the peer
         * (minus one-way network delay); a hard cap guarantees we
         * never freeze if the peer stops.  Gated on the DEBOUNCED
         * pause: a raw flag blip must never disable the stall. */
        if (handle->has_received_data && handle->has_connection && !peer_pause_confirmed && !myosd_is_paused()) {
            int stall_logged = 0;
            int stall_iters = 0;
            /* HARD depth cap: the advantage must never approach
             * the rollback window, or a peer confirmation falls
             * out of the ring / a rollback exceeds safe_depth ->
             * an uncorrectable permanent desync.  Derived from the
             * adaptive effective ring depth. */
            pthread_mutex_lock(&handle->sync_mutex);
            uint32_t fs0 = handle->frame_skip;
            pthread_mutex_unlock(&handle->sync_mutex);
            long safe_depth = (long)ROLLBACK_RING_FRAMES - 1 - (long)fs0;
            long hard_cap   = safe_depth - ROLLBACK_STALL_HARD_MARGIN;
            if (hard_cap < ROLLBACK_MAX_FRAME_ADVANTAGE + 1)
                hard_cap = ROLLBACK_MAX_FRAME_ADVANTAGE + 1; /* sanity */
            while (handle->has_connection && !peer_pause_confirmed_live() && !myosd_is_paused() &&
                   !handle->resync_active /* join the resync episode promptly */) {
                pthread_mutex_lock(&handle->sync_mutex);
                uint32_t lrpf = handle->last_received_peer_frame;
                uint32_t srtt = handle->min_rtt_window;
                uint32_t fs   = handle->frame_skip;
                /* lrpf is the PEER's target_frame = peer's frame +
                 * the PEER's input delay, which with the adaptive
                 * delay can differ from ours -- convert with the
                 * peer's own advertised value. */
                uint32_t pfs  = handle->peer_frame_skip ? handle->peer_frame_skip : fs;
                uint32_t cur  = handle->frame;
                pthread_mutex_unlock(&handle->sync_mutex);

                uint32_t peer_actual   = (lrpf > pfs) ? (lrpf - pfs) : 0;
                uint32_t half_rtt_frms = (srtt / 2) / 16; /* ms -> frames @60fps */
                long advantage = (long)cur - (long)peer_actual - (long)half_rtt_frms;
                if (advantage <= ROLLBACK_MAX_FRAME_ADVANTAGE) {
                    /* Dynamic rate control (RetroArch style): shed
                     * a sustained pacing mismatch by nudging MAME's
                     * speed factor (+/-0.5%, inaudible) instead of
                     * a visible full-frame stall.  EMA-smoothed,
                     * deadbanded so it idles at 1000 when healthy. */
                    g_rate_adv_ema_x16 += advantage - (g_rate_adv_ema_x16 / 16);
                    if (cur - g_rate_last_eval >= NETPLAY_RATE_EVAL_FRAMES) {
                        g_rate_last_eval = cur;
                        long adv_s = g_rate_adv_ema_x16 / 16;
                        int target = 1000;
                        if (adv_s > NETPLAY_RATE_DEADBAND_FRAMES)
                            target = 1000 - (int)(adv_s - NETPLAY_RATE_DEADBAND_FRAMES);
                        else if (adv_s < -NETPLAY_RATE_DEADBAND_FRAMES)
                            target = 1000 + (int)(-adv_s - NETPLAY_RATE_DEADBAND_FRAMES);
                        if (target < 1000 - NETPLAY_RATE_MAX_DELTA)
                            target = 1000 - NETPLAY_RATE_MAX_DELTA;
                        if (target > 1000 + NETPLAY_RATE_MAX_DELTA)
                            target = 1000 + NETPLAY_RATE_MAX_DELTA;
                        if (target != g_rate_speed) {
                            g_rate_speed += (target > g_rate_speed) ? 1 : -1;
                            myosd_netplay_set_speed(g_rate_speed);
                            NLOG("TELEM rate_ctl speed=%d adv_s=%ld adv=%ld rtt=%u role=%s",
                                 g_rate_speed, adv_s, advantage, srtt,
                                 handle->player1 ? "HOST" : "CLIENT");
                        }
                    }
                    break;
                }
                if (!stall_logged) {
                    NLOG("TELEM frame_advantage_stall cur=%u peer=%u adv=%ld rtt=%u role=%s",
                         cur, peer_actual, advantage, srtt, handle->player1 ? "HOST" : "CLIENT");
                    stall_logged = 1;
                }
                stall_iters++;
                /* SAFE zone: yield after the soft time cap for
                 * smoothness on small hip-ups.  HARD zone: NEVER
                 * yield -- keep stalling until the peer catches up
                 * or the disconnect timeout fires, so the
                 * advantage physically cannot cross safe_depth. */
                int in_hard_zone = (advantage >= hard_cap);
                if (!in_hard_zone && stall_iters > ROLLBACK_STALL_SOFT_ITERS)
                    break;
                if (in_hard_zone && stall_iters > ROLLBACK_STALL_DISCONNECT_ITERS) {
                    /* Peer has been frozen for seconds at the window edge:
                     * the session is dead.  Give up (freezing forever
                     * would ANR); a real desync here is preferable only
                     * because the alternative is a hung app, and the
                     * connection will time out separately.              */
                    NLOG("TELEM frame_advantage_stall_giveup adv=%ld hard_cap=%ld iters=%d role=%s (peer stalled at window edge)",
                         advantage, hard_cap, stall_iters, handle->player1 ? "HOST" : "CLIENT");
                    break;
                }
                struct timespec req = {0, 3000000}; // 3ms
                nanosleep(&req, NULL);
            }
        } else {
            /* No live sync running (paused / no peer data yet /
             * connection gone): return to 100% and reset the
             * controller so a stale rate cannot linger. */
            if (g_rate_speed != 1000) {
                g_rate_speed = 1000;
                myosd_netplay_set_speed(1000);
            }
            g_rate_adv_ema_x16 = 0;
        }

        /* Determinism watchdog (DEV, off): periodically force a null-rollback and
         * check the resim reproduces the forward CRC (local non-determinism probe).
         * Extra rollbacks + a whole-buffer CRC, so false for release. */
        constexpr bool RB_SELFCHECK_ENABLED = false;
        if (RB_SELFCHECK_ENABLED) {
            static uint32_t s_last_probe_frame = 0;
            const uint32_t PROBE_DEPTH    = 8;
            const uint32_t PROBE_INTERVAL = 300;
            uint32_t cur = handle->frame;
            if (!myosd_netplay_get_selfcheck_probe() &&
                !handle->requires_rollback &&
                cur > PROBE_DEPTH + handle->frame_skip + 2 &&
                cur - s_last_probe_frame >= PROBE_INTERVAL) {
                int win_ok = 1;
                uint32_t win_local_digital = 0, win_peer_digital = 0;
                for (uint32_t f = cur - PROBE_DEPTH; f < cur; f++) {
                    netplay_frame_history_t *fh =
                        &handle->frame_history[f % ROLLBACK_RING_FRAMES];
                    if (fh->frame != f || !fh->peer_confirmed) { win_ok = 0; break; }
                    win_local_digital |= fh->local_state.digital;
                    win_peer_digital  |= fh->peer_state.digital;
                }
                if (win_ok) {
                    s_last_probe_frame = cur;
                    myosd_netplay_set_selfcheck_probe(1);
                    pthread_mutex_lock(&handle->sync_mutex);
                    if (!handle->requires_rollback) {
                        handle->rollback_to_frame = cur - PROBE_DEPTH;
                        handle->requires_rollback = 1;
                        handle->rollback_arm_gen = handle->rollback_arm_gen + 1;   /* every arm bumps gen */
                    }
                    pthread_mutex_unlock(&handle->sync_mutex);
                    NLOG("RB_SELFCHECK probe armed: forcing null-rollback %u -> %u (local_dig=0x%x peer_dig=0x%x)",
                         cur, cur - PROBE_DEPTH, win_local_digital, win_peer_digital);
                }
            }
        }

        /* Normal rollback frame: save state + predict + inject. */
        netplay_pre_frame_net(handle);
        netplay_post_frame_net(handle);
        if (handle->player1)
            apply_netplay_input_state(is_new_mame_frame, 0, handle->state, 1, handle->peer_state);
        else
            apply_netplay_input_state(is_new_mame_frame, 1, handle->state, 0, handle->peer_state);
        netplay_trace_applied(handle->frame - 1, handle->state, handle->peer_state, "FWD");

        /* Post-resync phase diagnostic (see g_resync_phase_trace).  The netplay
         * frame just executed is handle->frame-1; pair it with the primary
         * screen's video frame_number so a cross-peer diff proves/locates a
         * 1-frame vblank/video phase slip after a drop-in. */
        if (g_resync_phase_trace > 0) {
            g_resync_phase_trace--;
            NLOG("RESYNC_PHASE role=%s netplay_frame=%u video_frame=%llu",
                 handle->player1 ? "HOST" : "CLIENT",
                 handle->frame - 1,
                 (unsigned long long)netplay_primary_screen_frame());
        }
    }
    return false;
}

/* ============================================================
 * SECTION 3 -- Savestate capture/restore bridge
 * Wired onto handle->rollback_* by netplay_iu_on_game_start (section 2,
 * above) and called every frame by the rollback engine in netplay.cpp
 * and by the trunk above.
 * ============================================================ */

/* Per-frame CRC + item-table trace (DEV, off): only RB_SELFCHECK needs it, to
 * name a diverging field; hashes every save_item every frame.  Off = the
 * detector computes its CRC just on its 5-frame cadence. */
static constexpr bool NETPLAY_PERFRAME_CRC_TRACE = false;

/* Forward declaration: the debug diagnostic defined in section 5 below is
 * only needed here, by state_capture. */
static uint32_t myosd_itemcrc_expanded_count(save_manager &save);

/* Which save_items feed the cross-peer detector CRC?  RAM ("memory/"), Neo Geo
 * battery state and chip-internal video RAM (save_hacks) -- offset-invariant.
 * CPU/peripheral state is skipped on purpose: with transfer off the peers anchor
 * frame 0 at different times, so time-derived state (timers, in-flight DMA/IRQ,
 * anything under ":maincpu") carries a bogus offset.  Real desyncs land in RAM. */
static bool myosd_netplay_crc_include_item(const char *name) {
    if (strncmp(name, "memory/", 7) == 0)      return true;  // RAM/shares/nvram -- the witness
    if (strstr(name, ":upd4990")  != nullptr)  return true;  // Neo Geo RTC (battery-backed)
    if (strstr(name, ":memcard")  != nullptr)  return true;  // Neo Geo memory card
    return myosd_save_hack_crc_include(name);                // chip video RAM
}

/* Item-aware CRC of ring slot `idx` (only myosd_netplay_crc_include_item
 * items included).  See the item-aware comment inside for the full walk. */
static uint32_t myosd_netplay_calc_crc(int idx) {
    std::lock_guard<std::recursive_mutex> rb_lock(g_rb_ring_mutex);
    if (!g_rb_states[idx]) return 0;
    auto const &data = g_rb_states[idx]->get_data();
    if (data.empty()) return 0;
    if (osdInterface == nullptr || !osdInterface->isMachine()) return 0;

    /* Item-aware CRC: walk the registered save_items in registration order and
     * feed only the gameplay-relevant ones into the running CRC, per the
     * generic include-list myosd_netplay_crc_include_item (memory/ blocks
     * only) -- excludes the noisy bytes interleaved throughout the blob. */
    constexpr size_t HEADER_SIZE = 32; /* matches save.cpp do_write header */
    save_manager &save = osdInterface->machine().save();
    int count = save.registration_count();

    util::crc32_creator crc;
    size_t offset = HEADER_SIZE;
    for (int i = 0; i < count; i++) {
        void *base; uint32_t valsize, valcount, blockcount, stride;
        const char *name = save.indexed_item(i, base, valsize, valcount, blockcount, stride);
        if (!name) break;
        if (myosd_slim_state::entry_class(save, (int)i) == myosd_slim_state::CLASS_EXCLUDE) continue; // dropped from the slim buffer
        size_t entry_size = (size_t)valsize * valcount * blockcount;
        if (entry_size && offset + entry_size <= data.size() &&
            myosd_netplay_crc_include_item(name))
            crc.append(data.data() + offset, (uint32_t)entry_size);
        offset += entry_size;
    }
    return crc.finish().m_raw;
}

/* True when this driver's harmless post-handover SH-2 phase blip trips the plain
 * detector (CPS-3), so netplay only warns there on a massive divergence.
 * Policy lives in save_hacks. */
bool myosd_netplay_desync_tolerant(void)
{
    if (osdInterface == nullptr || !osdInterface->isMachine()) return false;
    return myosd_save_hack_desync_tolerant(osdInterface->machine().system().type.source());
}

/* How much of `frame`'s slot could differ, in n*8 bits: the CRC include-list is
 * cut in blocks whose CRC parity lands in a bit picked by hashing the block
 * index, so the bits count differing blocks and ignore where they sit. */
void myosd_netplay_section_fingerprints(uint32_t frame, uint8_t *out, int n)
{
    for (int s = 0; s < n; s++) out[s] = 0;
    if (n <= 0) return;
    if (osdInterface == nullptr || !osdInterface->isMachine()) return;
    std::lock_guard<std::recursive_mutex> rb_lock(g_rb_ring_mutex);
    int idx = (int)(frame % ROLLBACK_RING_FRAMES);
    if (!g_rb_states[idx] || g_rb_frame_ids[idx] != frame) return;
    auto const &data = g_rb_states[idx]->get_data();
    if (data.empty()) return;

    constexpr size_t HEADER_SIZE = 32;
    save_manager &save = osdInterface->machine().save();
    int count = save.registration_count();
    const uint32_t buckets = (uint32_t)n * 8;

    util::crc32_creator crc;
    size_t   fill  = 0;
    uint32_t block = 0;
    auto flush = [&]() {
        uint32_t c = crc.finish().m_raw;
        uint32_t b = ((block + 1) * 2654435761u >> 8) % buckets;
        if (__builtin_parity(c)) out[b >> 3] ^= (uint8_t)(1u << (b & 7));
        crc.reset(); fill = 0; block++;
    };

    size_t offset = HEADER_SIZE;
    for (int i = 0; i < count; i++) {
        void *base; uint32_t vs, vc, bc, st;
        const char *name = save.indexed_item(i, base, vs, vc, bc, st);
        if (!name) break;
        if (myosd_slim_state::entry_class(save, (int)i) == myosd_slim_state::CLASS_EXCLUDE) continue;
        size_t entry_size = (size_t)vs * vc * bc;
        if (entry_size && offset + entry_size <= data.size() && myosd_netplay_crc_include_item(name)) {
            const uint8_t *p = data.data() + offset;
            size_t rem = entry_size;
            while (rem > 0) {
                size_t take = NETPLAY_CRC_BLOCK_BYTES - fill;
                if (take > rem) take = rem;
                crc.append(p, (uint32_t)take);
                p += take; rem -= take; fill += take;
                if (fill == NETPLAY_CRC_BLOCK_BYTES) flush();
            }
        }
        offset += entry_size;
    }
    if (fill > 0) flush();
}


/* Save current machine state into slot (frame % ROLLBACK_RING_FRAMES).    */
void myosd_netplay_state_capture(uint32_t frame)
{
    if (osdInterface == nullptr || !osdInterface->isMachine()) return;
    std::lock_guard<std::recursive_mutex> rb_lock(g_rb_ring_mutex);
    int idx = (int)(frame % ROLLBACK_RING_FRAMES);
    if (!g_rb_states[idx]) {
        g_rb_states[idx] = std::make_unique<myosd_slim_state>(osdInterface->machine().save());
    }

    /* device_post_load() callbacks (e.g. OKI M6295) mutate state on load and may
     * not be idempotent, so the slot keeps the pure pre-mutation `x` and the LIVE
     * machine is forced through the same mutation below -- both continue from
     * f(x).  RB_SELFCHECK (below) runs only during a forced null-rollback: a
     * bit-exact resim must reproduce the forward CRC, else it is local non-
     * determinism (logged once). */
    uint32_t selfcheck_old_frame_id = g_rb_frame_ids[idx];
    attotime selfcheck_old_time     = g_rb_times[idx]; /* FORWARD basetime @ this frame */

    /* Snapshot the FORWARD per-item CRC table before we overwrite the slot, so
     * that if the probe diverges we can name the exact save_items that differ
     * on THIS single device at the FIRST resimmed frame (root set).           */
    std::vector<uint32_t> selfcheck_old_table;
    if (g_rb_ff_active && g_rb_selfcheck_probe && selfcheck_old_frame_id == frame)
        selfcheck_old_table = g_rb_item_tables[idx];

    /* RB_SELFCHECK is a LOCAL probe (same device, same absolute time base), so
     * unlike the cross-peer detector it has NO boot-time offset -- it can and
     * MUST hash the WHOLE slim buffer (timers included) to catch rollback-
     * fidelity bugs the narrowed cross-peer include-list deliberately skips.
     * Snapshot the forward buffer's full CRC now, before save() below overwrites
     * the slot with the resim bytes. */
    uint32_t selfcheck_old_full  = 0;
    bool     selfcheck_have_full = false;
    if (g_rb_ff_active && g_rb_selfcheck_probe && selfcheck_old_frame_id == frame) {
        auto const &fdata = g_rb_states[idx]->get_data();
        if (!fdata.empty()) {
            selfcheck_old_full  = (uint32_t)util::crc32_creator::simple(fdata.data(), fdata.size());
            selfcheck_have_full = true;
        }
    }

    /* Time the capture: runs every emulated frame (forward and each
     * rollback-resim frame), so this is both the fixed netplay frame tax and
     * the per-frame cost of a rollback.  TELEM perf logs every ~300 captures
     * (~5s) for field profiling without a profiler. */
    static uint64_t s_perf_save_us = 0, s_perf_crc_us = 0;
    static uint32_t s_perf_save_max = 0, s_perf_crc_max = 0;
    static uint32_t s_perf_n = 0, s_perf_crc_n = 0;
    auto perf_t0 = std::chrono::steady_clock::now();

    g_rb_states[idx]->save(); // Slot = x
    g_rb_frame_ids[idx] = frame;

    auto perf_t1 = std::chrono::steady_clock::now();

    /* Whole-state CRC only if the detector is enabled and on its cadence
     * (NETPLAY_CRC_FRAME_WANTED, see netplay.h); 0 means "not computed" and
     * is treated as unavailable by state_checksum / the receive-side compare.
     * Must be written on every capture, since the slot now holds a different
     * frame's bytes than any previously cached CRC. */
    if (NETPLAY_PERFRAME_CRC_TRACE || NETPLAY_CRC_FRAME_WANTED(frame)) {
        g_rb_crcs[idx] = myosd_netplay_calc_crc(idx); // CRC(x) calculated BEFORE mutation
        uint32_t crc_us = (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - perf_t1).count();
        s_perf_crc_us += crc_us;
        if (crc_us > s_perf_crc_max) s_perf_crc_max = crc_us;
        s_perf_crc_n++;
    } else {
        g_rb_crcs[idx] = 0;
    }
    g_rb_times[idx] = osdInterface->machine().time(); // scheduler basetime @ frame start

    {
        uint32_t save_us = (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(
                               perf_t1 - perf_t0).count();
        s_perf_save_us += save_us;
        if (save_us > s_perf_save_max) s_perf_save_max = save_us;
        if (++s_perf_n >= 300) {
            NLOG("TELEM perf save_avg_us=%u save_max_us=%u crc_avg_us=%u crc_max_us=%u crc_n=%u state_kb=%u",
                (uint32_t)(s_perf_save_us / s_perf_n), s_perf_save_max,
                s_perf_crc_n ? (uint32_t)(s_perf_crc_us / s_perf_crc_n) : 0, s_perf_crc_max,
                s_perf_crc_n,
                (uint32_t)(g_rb_states[idx]->get_data().size() / 1024));
            s_perf_save_us = s_perf_crc_us = 0;
            s_perf_save_max = s_perf_crc_max = 0;
            s_perf_n = s_perf_crc_n = 0;
        }
    }

    /* Refresh this slot's per-item CRC table (forward during normal play,
     * resim during the probe) so the last few frames' tables are ready when
     * RB_SELFCHECK fires.  A full extra pass over the state blob every frame,
     * needed only by that probe, so compiled out unless the trace flag is set
     * (the cross-device ITEM_DIFF probe recomputes tables on demand instead). */
    if (NETPLAY_PERFRAME_CRC_TRACE) {
        uint32_t exp = myosd_itemcrc_expanded_count(osdInterface->machine().save());
        g_rb_item_tables[idx].resize(exp);
        myosd_netplay_get_item_crc_table(frame, g_rb_item_tables[idx].data(), exp);
    }

    if (g_rb_ff_active && g_rb_selfcheck_probe) {
        static uint32_t s_sc_last_ff_frame = 0;
        static int      s_sc_logged        = 0;
        if (frame != s_sc_last_ff_frame + 1) s_sc_logged = 0; /* new episode   */
        uint32_t selfcheck_new_full = 0;
        if (selfcheck_have_full) {
            auto const &rdata = g_rb_states[idx]->get_data();
            if (!rdata.empty())
                selfcheck_new_full = (uint32_t)util::crc32_creator::simple(rdata.data(), rdata.size());
        }
        if (!s_sc_logged && selfcheck_have_full &&
            selfcheck_old_full != selfcheck_new_full) {
            __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay",
                "RB_SELFCHECK frame=%u fwd_crc=0x%08x resim_crc=0x%08x DIVERGES (local rollback non-determinism, zero-input)",
                frame, selfcheck_old_full, selfcheck_new_full);
            /* Basetime at this frame start, forward vs resim: a difference
             * means the frame length (not the saved state) is the seed of
             * divergence.  Raw basetime is logged too, since machine().time()
             * inside a vblank callback returns its expire, not m_basetime. */
            {
                attotime tnow = osdInterface->machine().time();
                attotime bt_now = osdInterface->machine().scheduler().basetime();
                __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay",
                    "RB_TIME frame=%u fwd=%d.%018lld resim=%d.%018lld basetime_now=%d.%018lld %s",
                    frame,
                    selfcheck_old_time.seconds(), (long long)selfcheck_old_time.attoseconds(),
                    tnow.seconds(),               (long long)tnow.attoseconds(),
                    bt_now.seconds(),             (long long)bt_now.attoseconds(),
                    (tnow == selfcheck_old_time) ? "SAME-TIME(frame-length OK, seed elsewhere)"
                                                 : "TIME-DIVERGES(frame advanced different emu-time)");
            }
            /* Name the exact diverging save_items: resim table (local) vs
             * forward table (peer).  This is the single-device root set.       */
            if (!selfcheck_old_table.empty())
                myosd_netplay_diff_item_crc_tables(frame,
                    g_rb_item_tables[idx].data(), (uint32_t)g_rb_item_tables[idx].size(),
                    selfcheck_old_table.data(),   (uint32_t)selfcheck_old_table.size());
            s_sc_logged = 1;
        } else if (!s_sc_logged && selfcheck_have_full &&
                   selfcheck_old_full == selfcheck_new_full) {
            __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay",
                "RB_SELFCHECK frame=%u crc=0x%08x ok", frame, selfcheck_new_full);
        }
        s_sc_last_ff_frame = frame;
    }

    // NO RE-SAVE! Slot retains the pure `x`.
}

/* Restore the machine state that was captured for 'frame'.               */
void myosd_netplay_state_load(uint32_t frame)
{
    if (osdInterface == nullptr || !osdInterface->isMachine()) return;
    std::lock_guard<std::recursive_mutex> rb_lock(g_rb_ring_mutex);
    int idx = (int)(frame % ROLLBACK_RING_FRAMES);
    if (!g_rb_states[idx] || g_rb_frame_ids[idx] != frame) {
        /* The requested slot is not in the ring (evicted, or never captured).
         * We must NOT silently continue: the caller armed a rollback/FF against
         * this frame, so running on without rewinding diverges silently.  Log it
         * loudly; a proper recovery (cancel the FF) needs the load callback to
         * report failure, which the current void signature can't. */
        NLOG("RB_LOAD MISS frame=%u slot_has=%u ff_active=%d - NOT rewound (would desync)",
             frame, g_rb_frame_ids[idx], g_rb_ff_active);
        return;
    }
    {
        g_rb_states[idx]->load();

        /* MAME "quanta leak" determinism fix: the temporary quantum list is not
         * part of a savestate, so a load leaves stale temporary quanta against
         * the rewound basetime -- clear them to the canonical minimum. */
        osdInterface->machine().scheduler().clear_temporary_quanta();

        /* This function is called only from myosd_netplay_service_deferred_
         * load(), from running_machine::run() between timeslices, where
         * m_callback_timer==null and m_executing_device==null.  Loading at
         * this clean boundary (like MAME's own handle_saveload) leaves both
         * m_basetime and the timer list correctly rewound.  No re-save: the
         * clean-boundary capture already stored the canonical state. */
    }
}

/* Store a raw peer-supplied state buffer into the ring slot and load it into
 * the live machine immediately (boot-time initial sync only, see below). */
void myosd_netplay_state_inject(uint32_t frame, const uint8_t *buffer, uint32_t size)
{
    if (osdInterface == nullptr || !osdInterface->isMachine()) return;
    std::lock_guard<std::recursive_mutex> rb_lock(g_rb_ring_mutex);
    int idx = (int)(frame % ROLLBACK_RING_FRAMES);
    if (!g_rb_states[idx]) {
        g_rb_states[idx] = std::make_unique<myosd_slim_state>(osdInterface->machine().save());
    }

    /* Store the RAW (pre-load) buffer, never re-save after load(): load then
     * save is divergent (f(f(x)) != f(x)), so both peers must apply load()
     * exactly once to the same raw buffer to reach the same state.  The
     * canonical (post-load) state is written by the very next state_capture
     * call, as the frame-0 ring buffer entry. */
    g_rb_states[idx]->set_data(buffer, size);
    g_rb_frame_ids[idx] = frame;
    /* CRC only if the desync detector (or per-frame trace) is on. */
    g_rb_crcs[idx] = (NETPLAY_CRC_DETECTOR_ENABLED || NETPLAY_PERFRAME_CRC_TRACE)
                         ? myosd_netplay_calc_crc(idx) : 0;

    /* Apply the state to the live machine. ONE load, no re-save. */
    g_rb_states[idx]->load();
    osdInterface->machine().scheduler().clear_temporary_quanta();
}

/* Store-only variant of state_inject: writes the raw buffer without loading
 * it.  Used by the mid-game RESYNC, whose actual load happens later at a
 * clean scheduler boundary via the deferred-load path -- an in-timeslice
 * load mid-game corrupts the rewound timers against the live basetime. */
void myosd_netplay_state_store(uint32_t frame, const uint8_t *buffer, uint32_t size)
{
    if (osdInterface == nullptr || !osdInterface->isMachine()) return;
    std::lock_guard<std::recursive_mutex> rb_lock(g_rb_ring_mutex);
    int idx = (int)(frame % ROLLBACK_RING_FRAMES);
    if (!g_rb_states[idx]) {
        g_rb_states[idx] = std::make_unique<myosd_slim_state>(osdInterface->machine().save());
    }
    g_rb_states[idx]->set_data(buffer, size);
    g_rb_frame_ids[idx] = frame;
    g_rb_crcs[idx] = (NETPLAY_CRC_DETECTOR_ENABLED || NETPLAY_PERFRAME_CRC_TRACE)
                         ? myosd_netplay_calc_crc(idx) : 0;
}
/* Calculate CRC32 of the machine state that was captured for 'frame'.    */
uint32_t myosd_netplay_state_checksum(uint32_t frame)
{
    if (osdInterface == nullptr || !osdInterface->isMachine()) return 0;
    std::lock_guard<std::recursive_mutex> rb_lock(g_rb_ring_mutex);
    int idx = (int)(frame % ROLLBACK_RING_FRAMES);
    if (g_rb_states[idx] && g_rb_frame_ids[idx] == frame) {
        return g_rb_crcs[idx];
    }
    return 0;
}

/* NOTE: the returned pointer outlives the lock below, so it is NOT race-safe
 * against a concurrent state_capture() on the same slot -- callers (see the
 * sync capture in netplay_initial_sync, netplay.cpp) must copy the bytes
 * immediately, before any other ring access can happen. */
const uint8_t* myosd_netplay_get_state_buffer(uint32_t frame)
{
    if (osdInterface == nullptr || !osdInterface->isMachine()) return nullptr;
    std::lock_guard<std::recursive_mutex> rb_lock(g_rb_ring_mutex);
    int idx = (int)(frame % ROLLBACK_RING_FRAMES);
    if (g_rb_states[idx] && g_rb_frame_ids[idx] == frame) {
        auto const &data = g_rb_states[idx]->get_data();
        if (data.empty()) return nullptr;
        return (const uint8_t*)data.data();
    }
    return nullptr;
}

/* Release all saved states (called when the game exits).                 */
void myosd_netplay_state_cleanup()
{
    std::lock_guard<std::recursive_mutex> rb_lock(g_rb_ring_mutex);
    for (int i = 0; i < ROLLBACK_MAX_FRAMES; i++)
        g_rb_states[i].reset();
    /* Drop the cached per-entry classification so the next game rebuilds it. */
    myosd_slim_state::invalidate_class_cache();
}

/* Return the byte size of a single savestate. Used to gate rollback on
 * large-state machines (> ROLLBACK_STATE_SIZE_LIMIT).                    */
size_t myosd_netplay_get_state_size()
{
    if (osdInterface == nullptr || !osdInterface->isMachine()) return 0;
    return myosd_slim_state::get_size(osdInterface->machine().save());
}

/* Name of the system BIOS the RUNNING machine resolved to (e.g. "unibios40"),
 * or "" if the driver has no selectable BIOS.  Used to PIN the BIOS across a
 * netplay drop-in: the host is already running with a chosen BIOS, so it can
 * append it to the game name it hands the joiner ("mslug3;unibios40"), and the
 * joiner boots the same BIOS instead of being asked (which would let it pick a
 * different one and desync).  C-linkage so myosd_droid.cpp can call it. */
extern "C" const char *myosd_netplay_get_running_bios_name(void)
{
    static std::string s_bios;
    s_bios.clear();
    if (osdInterface == nullptr || !osdInterface->isMachine())
        return s_bios.c_str();
    device_t &root = osdInterface->machine().root_device();
    u8 sb = root.system_bios();               /* resolved BIOS index (bios_flags) */
    for (const rom_entry &rom : root.rom_region_vector())
    {
        if (ROMENTRY_ISSYSTEM_BIOS(&rom) && ROM_GETBIOSFLAGS(&rom) == sb)
        {
            s_bios = ROM_GETNAME(&rom);
            break;
        }
    }
    return s_bios.c_str();
}

/* ============================================================
 * SECTION 4 -- FF / audio / speed / selfcheck control accessors
 * Thin control surface over the section-1 flags; read by myosd_droid.cpp's
 * video/audio callbacks and by the trunk's dynamic rate controller.
 * ============================================================ */

/* Control the fast-forward suppression flag.                             */
void myosd_netplay_set_ff_active(int active)
{
    g_rb_ff_active = active;
    if (osdInterface != nullptr && osdInterface->isMachine()) {

        /* FF replay must be fast AND deterministic: throttle off so the
         * replay is nearly instantaneous, frameskip pinned to 0 so
         * screen_update() still runs every replayed frame on both peers.
         * Left untouched during RB_SELFCHECK so resim shares the forward
         * pass's exact video/timing path. */
        if (!g_rb_selfcheck_probe) {
            osdInterface->machine().video().set_frameskip(0);
            osdInterface->machine().video().set_throttled(!active);
        }
    }
}

/* Query the fast-forward suppression flag set above. */
int myosd_netplay_get_ff_active()
{
    return g_rb_ff_active;
}

/* Control audio muting during rollback fast-forward.                     */
void myosd_netplay_set_audio_mute(int mute)
{
    g_rb_audio_mute = mute;
}

/* Query the audio mute flag set above. */
int myosd_netplay_get_audio_mute()
{
    return g_rb_audio_mute;
}

/* Apply a machine speed factor (per-mille, 1000 = 100%); used by the
 * dynamic rate controller in myosd_netplay_input_update(). */
void myosd_netplay_set_speed(int speed)
{
    if (osdInterface != nullptr && osdInterface->isMachine()) {
        osdInterface->machine().video().set_speed_factor(speed);
    }
}

/* Debug: arm/disarm the rollback determinism self-check (see state_capture). */
void myosd_netplay_set_selfcheck_probe(int on)
{
    g_rb_selfcheck_probe = on;
}

/* Query the self-check probe flag set above. */
int myosd_netplay_get_selfcheck_probe()
{
    return g_rb_selfcheck_probe;
}

/* ============================================================
 * SECTION 5 -- Desync detector / per-item CRC diagnostics (debug-only)
 * Not part of the normal per-frame hot path.  myosd_itemcrc_expanded_count
 * is forward-declared in section 3 for state_capture to call.
 * ============================================================ */

/* Internal helper to compute CRC safely */
void myosd_netplay_log_sectional_crc(uint32_t frame)
{
    if (!NETPLAY_LOG_ENABLED) return; // DEV forensics only; no work in release
    if (osdInterface == nullptr || !osdInterface->isMachine()) return;
    std::lock_guard<std::recursive_mutex> rb_lock(g_rb_ring_mutex);
    int idx = (int)(frame % ROLLBACK_RING_FRAMES);
    if (!g_rb_states[idx] || g_rb_frame_ids[idx] != frame) {
        __android_log_print(ANDROID_LOG_ERROR, "MAME4droid_Netplay",
            "SECTIONAL_CRC: frame=%u not in slot %d (has frame %u)",
            frame, idx, g_rb_frame_ids[idx]);
        return;
    }

    auto const &data = g_rb_states[idx]->get_data();
    if (data.empty()) return;

    constexpr size_t TIMER_DATA_TAIL  = 1024;
    size_t safe_size = (data.size() > TIMER_DATA_TAIL)
                       ? data.size() - TIMER_DATA_TAIL
                       : data.size();
    constexpr int N_SECTIONS = 8;   // 8 sections
    size_t section_size = safe_size / N_SECTIONS;

    __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay",
        "SECTIONAL_CRC: frame=%u total_safe=%zu section_size=%zu",
        frame, safe_size, section_size);

    for (int s = 0; s < N_SECTIONS; s++) {
        size_t offset = s * section_size;
        size_t len    = (s == N_SECTIONS - 1)
                        ? (safe_size - offset)   // last section gets remainder
                        : section_size;
        uint32_t crc = util::crc32_creator::simple(
                            data.data() + offset, len);
        __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay",
            "SECTIONAL_CRC:   section[%d] offset=0x%06zx len=%zu crc=0x%08x",
            s, offset, len, crc);
    }
}

/* Cross-device per-item CRC diff (automated desync root-cause): get_item_crc_
 * table fills one CRC per save_item for `frame`; diff_item_crc_table logs the
 * items whose CRC differs from the peer's, by name.  Big items (e.g. 64KB
 * mainram) are split into sub-blocks so a diff localizes within the item
 * ("mainram +0x3400"); producer and both consumers must use the same rule. */
#define ITEMCRC_SUBBLOCK_THRESHOLD 2048
#define ITEMCRC_SUBBLOCK_SIZE      1024
static inline size_t myosd_itemcrc_subblock(size_t entry_size)
{
    return (entry_size > ITEMCRC_SUBBLOCK_THRESHOLD) ? ITEMCRC_SUBBLOCK_SIZE : 0;
}

/* Number of CRC entries the producer/consumers emit AFTER sub-block expansion.
 * This is >= registration_count() because large items are split, so the count
 * sanity check must use THIS, not registration_count() (which gave a spurious
 * "ITEM COUNT MISMATCH"). */
static uint32_t myosd_itemcrc_expanded_count(save_manager &save)
{
    uint32_t count = (uint32_t)save.registration_count();
    uint32_t k = 0;
    for (uint32_t i = 0; i < count; i++) {
        void *base; uint32_t valsize, valcount, blockcount, stride;
        const char *name = save.indexed_item((int)i, base, valsize, valcount, blockcount, stride);
        if (!name) break;
        if (myosd_slim_state::entry_class(save, (int)i) == myosd_slim_state::CLASS_EXCLUDE) continue; // dropped from the slim buffer
        size_t entry_size = (size_t)valsize * valcount * blockcount;
        size_t sub = myosd_itemcrc_subblock(entry_size);
        if (sub == 0) { k++; }
        else { for (size_t o = 0; o < entry_size; o += sub) k++; }
    }
    return k;
}

/* Fill `out` with one CRC per registered save_item of the ring slot for
 * `frame` (sub-block expanded).  Returns the count written, 0 if invalid. */
uint32_t myosd_netplay_get_item_crc_table(uint32_t frame, uint32_t *out, uint32_t max_items)
{
    if (osdInterface == nullptr || !osdInterface->isMachine()) return 0;
    std::lock_guard<std::recursive_mutex> rb_lock(g_rb_ring_mutex);
    int idx = (int)(frame % ROLLBACK_RING_FRAMES);
    if (!g_rb_states[idx] || g_rb_frame_ids[idx] != frame) return 0;

    auto const &data = g_rb_states[idx]->get_data();
    if (data.empty()) return 0;

    constexpr size_t HEADER_SIZE = 32; /* matches save.cpp do_write header */
    save_manager &save = osdInterface->machine().save();
    int count = save.registration_count();

    size_t offset = HEADER_SIZE;
    uint32_t written = 0;
    for (int i = 0; i < count; i++) {
        void *base; uint32_t valsize, valcount, blockcount, stride;
        const char *name = save.indexed_item(i, base, valsize, valcount, blockcount, stride);
        if (!name) break;
        if (myosd_slim_state::entry_class(save, (int)i) == myosd_slim_state::CLASS_EXCLUDE) continue; // dropped from the slim buffer
        size_t entry_size = (size_t)valsize * valcount * blockcount;
        size_t sub = myosd_itemcrc_subblock(entry_size);
        if (sub == 0) {
            if (written >= max_items) break;
            uint32_t crc = 0;
            if (entry_size && offset + entry_size <= data.size())
                crc = (uint32_t)util::crc32_creator::simple(data.data() + offset, entry_size);
            out[written++] = crc;
        } else {
            for (size_t o = 0; o < entry_size; o += sub) {
                if (written >= max_items) break;
                size_t bs = (entry_size - o < sub) ? (entry_size - o) : sub;
                uint32_t crc = 0;
                if (offset + o + bs <= data.size())
                    crc = (uint32_t)util::crc32_creator::simple(data.data() + offset + o, bs);
                out[written++] = crc;
            }
        }
        offset += entry_size;
    }
    return written;
}

/* Compute our own slot's per-item CRCs for `frame` and log only the items
 * whose CRC differs from the peer's table, by name. */
void myosd_netplay_diff_item_crc_table(uint32_t frame, const uint32_t *peer, uint32_t peer_count)
{
    if (osdInterface == nullptr || !osdInterface->isMachine()) return;
    std::lock_guard<std::recursive_mutex> rb_lock(g_rb_ring_mutex);
    int idx = (int)(frame % ROLLBACK_RING_FRAMES);
    if (!g_rb_states[idx] || g_rb_frame_ids[idx] != frame) {
        __android_log_print(ANDROID_LOG_ERROR, "MAME4droid_Netplay",
            "ITEM_DIFF: frame=%u no longer in ring (slot %d has %u) - cannot diff",
            frame, idx, g_rb_frame_ids[idx]);
        return;
    }

    auto const &data = g_rb_states[idx]->get_data();
    if (data.empty()) return;

    constexpr size_t HEADER_SIZE = 32;
    save_manager &save = osdInterface->machine().save();
    uint32_t count = (uint32_t)save.registration_count();
    uint32_t expanded = myosd_itemcrc_expanded_count(save);

    if (expanded != peer_count) {
        __android_log_print(ANDROID_LOG_ERROR, "MAME4droid_Netplay",
            "ITEM_DIFF: ITEM COUNT MISMATCH local=%u peer=%u (different driver/build/config!)",
            expanded, peer_count);
    }

    __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay",
        "ITEM_DIFF: ===== begin diff frame=%u items=%u =====", frame, count);

    size_t offset = HEADER_SIZE;
    int diffs = 0;
    uint32_t k = 0;   /* emitted-CRC index (must match producer expansion) */
    for (uint32_t i = 0; i < count; i++) {
        void *base; uint32_t valsize, valcount, blockcount, stride;
        const char *name = save.indexed_item((int)i, base, valsize, valcount, blockcount, stride);
        if (!name) break;
        if (myosd_slim_state::entry_class(save, (int)i) == myosd_slim_state::CLASS_EXCLUDE) continue; // dropped from the slim buffer
        size_t entry_size = (size_t)valsize * valcount * blockcount;
        size_t sub = myosd_itemcrc_subblock(entry_size);
        if (sub == 0) {
            uint32_t local_crc = 0;
            if (entry_size && offset + entry_size <= data.size())
                local_crc = (uint32_t)util::crc32_creator::simple(data.data() + offset, entry_size);
            if (k < peer_count && local_crc != peer[k]) {
                __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay",
                    "ITEM_DIFF: off=0x%06zx sz=%-6zu local=0x%08x peer=0x%08x %s",
                    offset, entry_size, local_crc, peer[k], name);
                diffs++;
            }
            k++;
        } else {
            for (size_t o = 0; o < entry_size; o += sub, k++) {
                size_t bs = (entry_size - o < sub) ? (entry_size - o) : sub;
                uint32_t local_crc = 0;
                if (offset + o + bs <= data.size())
                    local_crc = (uint32_t)util::crc32_creator::simple(data.data() + offset + o, bs);
                if (k < peer_count && local_crc != peer[k]) {
                    __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay",
                        "ITEM_DIFF: off=0x%06zx sz=%-6zu local=0x%08x peer=0x%08x %s +0x%04zx",
                        offset + o, bs, local_crc, peer[k], name, o);
                    diffs++;
                }
            }
        }
        offset += entry_size;
    }
    __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay",
        "ITEM_DIFF: ===== end diff frame=%u: %d differing item(s) =====", frame, diffs);
}

/* Diff two precomputed per-item CRC tables (local snapshot vs peer), naming
 * each differing item from save_manager's stable registration order.  Unlike
 * diff_item_crc_table this does not read the rollback ring, so it survives
 * ring eviction: the client snapshots its own table at desync-detect time and
 * diffs it once the host's table finishes arriving over a slow link. */
void myosd_netplay_diff_item_crc_tables(uint32_t frame,
        const uint32_t *local, uint32_t local_count,
        const uint32_t *peer,  uint32_t peer_count)
{
    if (osdInterface == nullptr || !osdInterface->isMachine()) return;

    constexpr size_t HEADER_SIZE = 32;
    save_manager &save = osdInterface->machine().save();
    uint32_t count = (uint32_t)save.registration_count();

    if (local_count != peer_count) {
        __android_log_print(ANDROID_LOG_ERROR, "MAME4droid_Netplay",
            "ITEM_DIFF: ITEM COUNT MISMATCH regcount=%u local=%u peer=%u (different driver/build/config?)",
            count, local_count, peer_count);
    }

    __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay",
        "ITEM_DIFF: ===== begin diff frame=%u items=%u (snapshot vs peer) =====", frame, count);

    size_t offset = HEADER_SIZE;
    int diffs = 0;
    uint32_t n = local_count < peer_count ? local_count : peer_count;
    uint32_t k = 0;   /* emitted-CRC index (must match producer expansion) */
    for (uint32_t i = 0; i < count; i++) {
        void *base; uint32_t valsize, valcount, blockcount, stride;
        const char *name = save.indexed_item((int)i, base, valsize, valcount, blockcount, stride);
        if (!name) break;
        if (myosd_slim_state::entry_class(save, (int)i) == myosd_slim_state::CLASS_EXCLUDE) continue; // dropped from the slim buffer
        size_t entry_size = (size_t)valsize * valcount * blockcount;
        size_t sub = myosd_itemcrc_subblock(entry_size);
        if (sub == 0) {
            if (k < n && local[k] != peer[k]) {
                __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay",
                    "ITEM_DIFF: off=0x%06zx sz=%-6zu local=0x%08x peer=0x%08x %s",
                    offset, entry_size, local[k], peer[k], name);
                diffs++;
            }
            k++;
        } else {
            for (size_t o = 0; o < entry_size; o += sub, k++) {
                size_t bs = (entry_size - o < sub) ? (entry_size - o) : sub;
                if (k < n && local[k] != peer[k]) {
                    __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay",
                        "ITEM_DIFF: off=0x%06zx sz=%-6zu local=0x%08x peer=0x%08x %s +0x%04zx",
                        offset + o, bs, local[k], peer[k], name, o);
                    diffs++;
                }
            }
        }
        offset += entry_size;
    }
    __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay",
        "ITEM_DIFF: ===== end diff frame=%u: %d differing item(s) =====", frame, diffs);
}

/* ============================================================
 * SECTION 6 -- Applied-input trace (debug-only, off by default)
 * netplay_trace_applied is forward-declared in section 2 for
 * netplay_iu_rollback_ff_step / netplay_iu_rollback_normal_step to call.
 * ============================================================ */

/* Applied-input trace (desync diagnosis): records the (local,peer) digital
 * input injected into MAME per frame into an in-memory ring, tagging FF
 * replay vs the normal forward path.  Arming is OFF by default
 * (APPLIED_TRACE_ENABLED below); diffing the "APPLIED" lines around a
 * trigger names the frame the rollback re-simulation got wrong. */
#define APPLIED_RING_SIZE 256          /* total entries kept (pre+post trigger) */
#define APPLIED_POST_ENTRIES 80        /* entries to capture AFTER the trigger  */

typedef struct {
    uint32_t    frame;
    uint32_t    local;
    uint32_t    peer;
    /* Hash of the WHOLE applied state, not just the digital bits: every
     * mismatch seen so far was on an analog axis with identical digitals,
     * which a digital-only trace cannot see at all. */
    uint32_t    local_h;
    uint32_t    peer_h;
    const char *src;
} applied_entry_t;

/* Order-dependent mix over every field netplay_state_differs compares, so two
 * peers that applied the same input produce the same number. */
static uint32_t netplay_state_hash(const netplay_state_t &s)
{
    uint32_t h = 2166136261u;
    const unsigned char *p = (const unsigned char *)&s;
    for (size_t i = 0; i < sizeof(netplay_state_t); i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static applied_entry_t g_applied_ring[APPLIED_RING_SIZE];
static uint32_t        g_applied_head = 0;     /* next write slot (monotonic)   */
static volatile int    g_applied_arm  = 0;     /* 1 = trigger seen, counting down */
static int             g_applied_arm_high = 0; /* armed by a high-prio trigger   */
static int             g_applied_post = 0;     /* entries left before flush      */
static int             g_applied_done = 0;     /* at least one dump has happened */
static int             g_applied_desync_done = 0; /* the DESYNC dump has its own turn */
static uint32_t        g_applied_last_dump_frame = 0; /* frame of the last flush */
static uint32_t        g_applied_trigger_frame = 0;
static const char     *g_applied_trigger_reason = "";

/* Per-episode, not permanently one-shot: a true one-shot would burn its
 * single capture on an early, benign jitter mismatch that self-corrects, and
 * stay silent for the rare real cascading desync that happens later.  This
 * cooldown lets it capture again for a later, separate episode. */
#define APPLIED_REARM_COOLDOWN_FRAMES 3000

/* Dump the whole applied-input ring to NLOG and reset the arm state. */
static void netplay_applied_ring_flush(void)
{
    g_applied_arm = 0;
    g_applied_done = 1;
    g_applied_last_dump_frame = g_applied_trigger_frame;
    uint32_t count = (g_applied_head < APPLIED_RING_SIZE) ? g_applied_head
                                                          : APPLIED_RING_SIZE;
    uint32_t start = g_applied_head - count;    /* oldest still-retained entry   */
    NLOG("APPLIED_DUMP begin reason=%s trigger_frame=%u entries=%u",
         g_applied_trigger_reason, g_applied_trigger_frame, count);
    for (uint32_t k = 0; k < count; k++) {
        uint32_t i = (start + k) % APPLIED_RING_SIZE;
        NLOG("APPLIED f=%u local=0x%x peer=0x%x lh=0x%08x ph=0x%08x src=%s",
             g_applied_ring[i].frame, g_applied_ring[i].local,
             g_applied_ring[i].peer, g_applied_ring[i].local_h,
             g_applied_ring[i].peer_h, g_applied_ring[i].src);
    }
    NLOG("APPLIED_DUMP end trigger_frame=%u", g_applied_trigger_frame);
}

/* Record one applied (local,peer) digital input pair into the ring; flushes
 * once the post-trigger window closes (see netplay_applied_ring_arm). */
static inline void netplay_trace_applied(uint32_t frame,
                                         const netplay_state_t &loc,
                                         const netplay_state_t &peer,
                                         const char *src)
{
    uint32_t i = g_applied_head % APPLIED_RING_SIZE;
    g_applied_ring[i].frame   = frame;
    g_applied_ring[i].local   = loc.digital;
    g_applied_ring[i].peer    = peer.digital;
    g_applied_ring[i].local_h = netplay_state_hash(loc);
    g_applied_ring[i].peer_h  = netplay_state_hash(peer);
    g_applied_ring[i].src     = src;
    g_applied_head++;

    /* After a trigger, capture a short post-window (covers the FF replay) then
     * flush the whole ring exactly once.                                      */
    if (g_applied_arm && g_applied_post > 0 && --g_applied_post == 0)
        netplay_applied_ring_flush();
}

/* Called from the desync triggers (network thread): arms the ring flush; the
 * dump runs on the game thread once the post-window fills, so all NLOG I/O is on
 * one thread.  A HIGH trigger (ROLLBACK mismatch) upgrades an in-flight LOW arm.
 * A resync restarts at frame 0, so stale ring entries reuse the new frame
 * numbers -- drop them and re-arm. */
extern "C" void netplay_applied_ring_reset(void)
{
    g_applied_desync_done     = 0;
    g_applied_head            = 0;
    g_applied_arm             = 0;
    g_applied_arm_high        = 0;
    g_applied_post            = 0;
    g_applied_done            = 0;
    g_applied_last_dump_frame = 0;
}

extern "C" void netplay_applied_ring_arm(const char *reason, uint32_t frame)
{
    constexpr bool APPLIED_TRACE_ENABLED = false;
    if (!APPLIED_TRACE_ENABLED)
        return;

    /* "ROLLBACK_mismatch" starts with 'R'; "DESYNC" with 'D'.                */
    int high = (reason && reason[0] == 'R');

    /* A confirmed desync often IS the end of the session (the user stops
     * right there), and the deferred flush needs more traced frames that
     * never come.  Dump it now instead of arming and losing it.         */
    if (!high) {
        if (g_applied_desync_done)
            return;                 /* one desync dump per episode: the log is throttled, we are not */
        g_applied_desync_done = 1;
        g_applied_trigger_reason = reason;
        g_applied_trigger_frame  = frame;
        netplay_applied_ring_flush();
        return;
    }

    if (g_applied_arm) {
        /* Already counting down: only upgrade LOW->HIGH (restart post-window
         * so the dump is centred on the rollback, not the earlier desync).   */
        if (high && !g_applied_arm_high) {
            g_applied_arm_high       = 1;
            g_applied_trigger_reason = reason;
            g_applied_trigger_frame  = frame;
            g_applied_post           = APPLIED_POST_ENTRIES;
        }
        return;
    }

    /* A confirmed CRC desync is the event we are here for: never let the
     * cooldown swallow it.  The cooldown exists to throttle the noisy
     * ROLLBACK_mismatch trigger, nothing else.                          */
    /* Signed delta: `frame` is NOT monotonic across calls (netplay.cpp's
     * confirm loop walks newest->oldest within a packet), so unsigned
     * subtraction would underflow and defeat the cooldown below. */
    if (high && g_applied_done && (int32_t)(frame - g_applied_last_dump_frame) < APPLIED_REARM_COOLDOWN_FRAMES)
        return;                                     /* cooldown: too soon, skip */

    g_applied_arm_high       = high;
    g_applied_trigger_reason = reason;
    g_applied_trigger_frame  = frame;
    g_applied_post           = APPLIED_POST_ENTRIES;
    g_applied_arm            = 1;
}
