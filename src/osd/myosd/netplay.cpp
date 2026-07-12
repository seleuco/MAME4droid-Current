// license:BSD-3-Clause
// copyright-holders: David Valdeita (Seleuco)
/***************************************************************************

    netplay.cpp

   MAME4droid Netplay Architecture (Lock-Step + Rollback)

***************************************************************************/

/* ==========================================================================
 * MAME4droid Netplay Architecture (Lock-Step + Rollback)
 * ==========================================================================
 *
 * OVERVIEW:
 * Custom UDP-based netplay with two selectable execution models, chosen per
 * session (handle->mode) and defined in netplay.h (NETPLAY_MODE_LOCKSTEP /
 * NETPLAY_MODE_ROLLBACK). MAME emulation is entirely deterministic: two
 * peers stay in sync as long as they execute the exact same inputs on the
 * exact same frames from the exact same starting state. Both modes exist to
 * trade that guarantee against latency in different ways; ROLLBACK can also
 * fall back to LOCK-STEP automatically for drivers it cannot support safely
 * (see ROLLBACK FALLBACKS below).
 *
 * ── LOCK-STEP MODE ────────────────────────────────────────────────────────
 * The emulator cannot simulate frame N until it has received the peer's
 * input for frame N; if it hasn't arrived, the emulation thread blocks.
 *
 * JITTER BUFFER & INPUT LAG (FRAMESKIP):
 * To hide network latency and prevent constant blocking, the system uses a
 * predefined Input Lag, internally called `frame_skip` (or `target_delay`).
 * If the delay is 4 frames:
 *   - At frame 100, we sample the local joystick.
 *   - We send this input to the peer, tagged for execution at frame 104.
 *   - The emulator then executes frame 100 (using inputs sampled at frame 96).
 * As long as the network latency is lower than the time it takes to render
 * 4 frames (~66ms), the inputs arrive ahead of time, and the game never
 * stutters. Cost: every action is felt N frames later, always. Works with
 * ANY driver (no re-simulation, no dependency on deterministic replay) --
 * the universal, always-safe mode.
 *
 * ── ROLLBACK MODE ─────────────────────────────────────────────────────────
 * The emulator never blocks. Each frame it predicts the peer's input
 * (repeat last known), executes immediately with local + predicted input,
 * and captures the resulting machine state into a ring buffer of savestates
 * (myosd-netplay.cpp). When the peer's REAL input for a past frame finally
 * arrives and differs from the prediction, that frame is mispredicted: the
 * machine rolls back to the last correct ring-buffer slot and fast-forwards
 * back to the present, re-simulating every frame in between with the
 * now-correct input (deferred load at a clean scheduler boundary --
 * myosd_netplay_service_deferred_load). Correct predictions cost nothing; a
 * misprediction costs a fast, silent re-simulation instead of a network
 * stall, so a good connection feels input-lag-free. The trade-off is that
 * the driver's frame re-simulation must be bit-exact deterministic.
 *
 * Rollback determinism relies on the boot itself being pinned identical on
 * both peers (RTC epoch, sample rate, CHD diff wipe, nvram/cfg) rather than
 * on transferring the initial machine state over the wire: by default
 * (NETPLAY_ROLLBACK_INITIAL_STATE_TRANSFER=0, GGPO-style) only inputs cross
 * the network and both machines simulate the identical boot locally; a
 * local save is still taken at boot -- never transferred -- purely to size
 * the ring buffer (see ROLLBACK FALLBACKS). Session-time drift accumulated
 * by re-simulation cost is corrected by a Dynamic Rate Controller that
 * nudges the machine speed factor in small per-mille steps
 * (myosd_netplay_set_speed / video.cpp's throttle baseline rescale, so a
 * step costs nothing even under continuous stepping).
 *
 * ROLLBACK FALLBACKS (drivers that cannot re-simulate bit-exact):
 * - State-size gate: the boot-time local save sizes the per-slot state; if
 *   it exceeds ROLLBACK_STATE_SIZE_LIMIT, or the adaptive ring depth
 *   (min(ROLLBACK_MAX_FRAMES, RAM budget / state size)) would fall below
 *   ROLLBACK_MIN_FRAMES, the session falls back to LOCK-STEP.
 * - Cross-device layout probe: peers exchange their state size at boot
 *   (NETPLAY_MSG_STATE_SIZE); a mismatch (e.g. a sample-rate-dependent
 *   sound-chip layout) also falls back to LOCK-STEP rather than risk a
 *   silent desync.
 * - Mid-game RESYNC (NETPLAY_MSG_RESYNC): a user-triggered recovery that
 *   re-synchronizes both peers from a freshly transferred state without
 *   restarting the session, for a driver that desyncs during play.
 * - CRC desync detector (myosd-netplay.cpp, NETPLAY_CRC_DETECTOR_ENABLED,
 *   diagnostic / off by default): periodically hashes every "memory/"
 *   save_item and compares peers, naming the exact diverging item when a
 *   driver turns out not to be rollback-safe.
 *
 * INTEGRATION WITH MAME:
 * 1. netplay_pre_frame_net():
 *    Called by the OSD layer just before MAME executes a video frame.
 *    LOCK-STEP: blocks until the peer's input for the current
 *    `target_frame` is available, then injects local + peer input.
 *    ROLLBACK: never blocks; captures state, predicts the peer's input
 *    (repeat-last), and injects local + predicted input immediately.
 *
 * 2. netplay_post_frame_net():
 *    Called immediately after MAME finishes executing the frame. Samples
 *    the new local input, prepares it for execution N frames in the future
 *    (LOCK-STEP) or transmits it right away (ROLLBACK), advances the frame
 *    counters, and sends the UDP packet to the peer. Also runs the
 *    Auto-Frameskip evaluation (LOCK-STEP) every 5 seconds.
 *
 * RESILIENCE (both modes):
 * - N-1 History: Every UDP packet contains the inputs for both the current
 *   frame and the previous frame. If a single packet is lost, the next
 *   packet transparently recovers the lost data, preventing emulation
 *   stalls.
 * - Auto-Frameskip (LOCK-STEP): Automatically adjusts the input lag
 *   (Jitter Buffer) based on real-time network conditions, finding the
 *   optimal sweet spot between smoothness and responsiveness.
 * - Build/protocol handshake: peers exchange a protocol version and the
 *   rollback limits (state-size cap, ring depth, RAM budget) at JOIN, and
 *   refuse a mismatched build rather than risk an undiagnosable desync.
 *
 * FILE LAYOUT (netplay.cpp), most central first:
 *   1. Core state & ubiquitous utilities (handle accessor, byte-order codec)
 *   2. Per-frame game-thread trunk (netplay_pre/post_frame_net)
 *   3. Network receive path (netplay_read_data)
 *   4. Network send path (protocol senders, netplay_send_data)
 *   5. Session lifecycle (init/mode/warnings/resync trigger/game-start
 *      bootstrap: netplay_track_connection/initial_sync/start_barrier)
 *   6. External predicates / UI hooks
 *
 * ==========================================================================
 */

#include <stdio.h>
#include "netplay.h"
#include "myosd-netplay.h"
#include <unistd.h>
#include <time.h>

#include <string.h>
#include <zlib.h>
#include <errno.h>

#include <netinet/in.h>
#include <android/log.h>

#define NLOG(...) do { if(NETPLAY_LOG_ENABLED) __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay", __VA_ARGS__); } while(0)
#define NLOG_VERBOSE(...) do { if(0) __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay", __VA_ARGS__); } while(0)

// Netplay C++ helper functions imported from myosd-droid.cpp
extern void myosd_droid_netplay_set_exitPause(int val);
extern void myosd_droid_netplay_force_pause();
extern int myosd_droid_netplay_get_inMenu();
extern int myosd_droid_netplay_get_ext_status();
extern unsigned long myosd_droid_netplay_joystick_read(int i);
extern float myosd_droid_netplay_joystick_read_analog(int i, char axis);
extern unsigned long myosd_droid_netplay_mouse_read(int i);
extern float myosd_droid_netplay_mouse_read_analog(int i, char axis);
extern float myosd_droid_netplay_lightgun_read_analog(int i, char axis);
/* Cross-peer sample-rate sync (see myosd-droid.cpp).                       */
extern int  myosd_droid_get_effective_sound_rate(void);
extern void myosd_droid_set_netplay_sound_rate(int rate);

/* ============================================================
 * SECTION 1 -- Core state & ubiquitous utilities
 * The singleton handle accessor, wall-clock ticks and the wire byte-order
 * codec used by both the receive path (section 3) and the send path
 * (section 4).
 * ============================================================ */

static netplay_t netplay_player;

/* Return the process-wide netplay handle (lazy-initialised singleton).    */
netplay_t * netplay_get_handle(){
    static int init = 0;    
    if(!init)
    {
        netplay_init_handle(&netplay_player);
        init = 1;
    }
    return &netplay_player;
}

/* Monotonic-ish wall clock in milliseconds, used for RTT/timeout math.    */
static uint32_t netplay_get_ticks_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
}

/* Byte-order helpers
 * We use memcpy so the compiler never generates an unaligned load/store
 * even inside pack(1) structs.  All multi-byte fields in netplay_msg_t
 * live at odd offsets due to pack(1), so plain cast-and-ntohl is UB on
 * strict-alignment platforms.                                              */

/* Host-order float -> network-order float, written via memcpy (see above). */
void htonf_inplace(float* dest, float value) {
    uint32_t temp;
    memcpy(&temp, &value, 4);
    temp = htonl(temp);
    memcpy(dest, &temp, 4);
}

/* Network-order float -> host-order float, read via memcpy (see above).   */
float ntohf_inplace(const float* src) {
    uint32_t temp;
    memcpy(&temp, src, 4);
    temp = ntohl(temp);
    float value;
    memcpy(&value, &temp, 4);
    return value;
}

/* Decode an entire netplay_state from a packed network message into a
 * host-order netplay_state_t.  Centralises the 13-field ntoh fan-out
 * that was duplicated three times in the original code.                    */
static void decode_peer_state(netplay_state_t *out, const netplay_state_t *in_net)
{
    out->digital      = ntohl(in_net->digital);
    out->analog_x     = ntohf_inplace(&in_net->analog_x);
    out->analog_y     = ntohf_inplace(&in_net->analog_y);
    out->analog_rx    = ntohf_inplace(&in_net->analog_rx);
    out->analog_ry    = ntohf_inplace(&in_net->analog_ry);
    out->analog_lz    = ntohf_inplace(&in_net->analog_lz);
    out->analog_rz    = ntohf_inplace(&in_net->analog_rz);
    out->mouse_status = ntohl(in_net->mouse_status);
    out->mouse_x      = ntohf_inplace(&in_net->mouse_x);
    out->mouse_y      = ntohf_inplace(&in_net->mouse_y);
    out->lightgun_x   = ntohf_inplace(&in_net->lightgun_x);
    out->lightgun_y   = ntohf_inplace(&in_net->lightgun_y);
    out->ext          = ntohs(in_net->ext);
}

/* Encode a host-order netplay_state_t into a packed network message.      */
static void encode_peer_state(netplay_state_t *out_net, const netplay_state_t *in)
{
    out_net->digital      = htonl(in->digital);
    htonf_inplace(&out_net->analog_x,   in->analog_x);
    htonf_inplace(&out_net->analog_y,   in->analog_y);
    htonf_inplace(&out_net->analog_rx,  in->analog_rx);
    htonf_inplace(&out_net->analog_ry,  in->analog_ry);
    htonf_inplace(&out_net->analog_lz,  in->analog_lz);
    htonf_inplace(&out_net->analog_rz,  in->analog_rz);
    out_net->mouse_status = htonl(in->mouse_status);
    htonf_inplace(&out_net->mouse_x,    in->mouse_x);
    htonf_inplace(&out_net->mouse_y,    in->mouse_y);
    htonf_inplace(&out_net->lightgun_x, in->lightgun_x);
    htonf_inplace(&out_net->lightgun_y, in->lightgun_y);
    out_net->ext          = htons(in->ext);
}

/* Effective rollback ring depth for the current game.  Computed by the
 * myosd-netplay.cpp size gate as min(ROLLBACK_MAX_FRAMES, BUDGET/state_size);
 * every frame->slot mapping and safe_depth derivation goes through the
 * ROLLBACK_RING_FRAMES macro (netplay.h) reading this.                      */
uint32_t myosd_netplay_ring_frames = ROLLBACK_MAX_FRAMES;

/* ============================================================
 * SECTION 2 -- Per-frame game-thread trunk
 * netplay_pre_frame_net() / netplay_post_frame_net() are called once per
 * MAME video frame from myosd-netplay.cpp's rollback step (and the
 * lockstep path), just before/after MAME executes the frame -- see the
 * file header's INTEGRATION WITH MAME section.
 * ============================================================ */

/* True when no blocking wait is needed: frame < target_frame (peer data not
 * yet required), or frame == target_frame and peer_frame caught up.  MUST be
 * evaluated while holding handle->sync_mutex.                             */
#define IS_SYNCED(h) \
    ( (h)->frame < (h)->target_frame || \
      ( (h)->frame == (h)->target_frame    && \
        (h)->peer_frame == (h)->target_frame ) )

/* Maximum total wait (ms) before declaring hangup.                         */
#define SYNC_TIMEOUT_MS      30000
/* How often we re-transmit while waiting (ms).  Replaces old 1 ms spin.   */
#define RETRANSMIT_MS        24

/* Called just before MAME executes a frame: LOCKSTEP blocks until the
 * peer's input is available; ROLLBACK never blocks, it predicts+injects.  */
void netplay_pre_frame_net(netplay_t *handle)
{
    if (!handle->has_connection || !handle->has_begun_game) return;

    /* Rollback mode: no blocking wait.  Snapshot state, predict the peer's
     * input (repeat-last), commit immediately.  The network thread may later
     * trigger a rollback via requires_rollback.                             */
    if (handle->mode == NETPLAY_MODE_ROLLBACK) {
        if (!handle->rollback_enabled) return;

        /* 1. Save machine state for this frame via bridge callback.  Do NOT
         * capture frame 0: slot 0 must retain the stable raw_A from initial
         * sync (capturing now would store an unsettled post-load state).   */
        if (handle->rollback_capture_state && handle->frame != 0)
            handle->rollback_capture_state(handle->frame);

        pthread_mutex_lock(&handle->sync_mutex);

        /* 2. Record local input in ring buffer.                           */
        int idx = (int)(handle->frame % ROLLBACK_RING_FRAMES);
        handle->frame_history[idx].frame          = handle->frame;
        handle->frame_history[idx].peer_confirmed = 0;

        /* INITIAL SYNC WAIT:
         * Wait at frame_skip for the peer's first frame. This prevents the Host
         * from running ahead and mispredicting the first few frames, which 
         * dramatically reduces the chance of an early rollback.
         * We wait until we have received the first packet from the peer. */
        if (handle->frame == handle->frame_skip && !handle->has_received_data) {
            NLOG("ROLLBACK: Waiting for peer's first frame to synchronize start...");
            while (handle->has_connection && !handle->has_received_data) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 1;
                pthread_cond_timedwait(&handle->sync_cond, &handle->sync_mutex, &ts);
            }
            NLOG("ROLLBACK: Initial sync complete!");
        }

        /* 3. Predict peer input: repeat last known confirmed input.
         *    On the very first frame use zeroed state (already zero
         *    from memset in netplay_init_handle).                         */
        netplay_state_t *pred = &handle->frame_history[idx].peer_state;
        if (handle->frame > 0) {
            int prev_idx = (int)((handle->frame - 1) % ROLLBACK_RING_FRAMES);
            *pred = handle->frame_history[prev_idx].peer_state;
        } else {
            memset(pred, 0, sizeof(*pred));
        }

        /* Override prediction if we received the actual input early.     */
        int early_idx = (int)(handle->frame % EARLY_BUFFER_SIZE);
        if (handle->early_peer_frame[early_idx] == handle->frame) {
            *pred = handle->early_peer_state[early_idx];
            handle->frame_history[idx].peer_confirmed = 1;
            handle->early_peer_frame[early_idx] = 0xFFFFFFFF; // consume
        }

        /* 4. Expose committed states via handle->state / handle->peer_state
         *    so that apply_netplay_input_state (myosd-netplay.cpp) works  */
        // handle->state is set in netplay_post_frame_net AFTER reading local input!
        handle->frame_history[idx].applied_peer_state = handle->frame_history[idx].peer_state;
        handle->peer_state = handle->frame_history[idx].applied_peer_state;

        pthread_mutex_unlock(&handle->sync_mutex);
        return;
    }
    /* ── END ROLLBACK MODE ──────────────────────────────────────────────── */

    if (handle->frame < handle->frame_skip)
        return; /* First frame_skip frames do not need a sync point.       */

    pthread_mutex_lock(&handle->sync_mutex);

    if (!IS_SYNCED(handle))
    {
        handle->timeout_cnt++;

        /* cond_timedwait-based sync wait: wakes immediately when the network
         * thread signals sync_cond, and retransmits every RETRANSMIT_MS ms
         * to drive the peer's ACK -- no busy-polling.                      */

        /* Overall deadline (30 s, extended for early frames).             */
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        long timeout_ms = SYNC_TIMEOUT_MS;
        if (handle->frame < 10) timeout_ms *= 8; /* extra time at start   */
        deadline.tv_sec  += timeout_ms / 1000;
        deadline.tv_nsec += (timeout_ms % 1000) * 1000000LL;
        if (deadline.tv_nsec >= 1000000000LL) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000LL;
        }

        int warn_paused = 0;
        int inner_iter = 0;

        while (!IS_SYNCED(handle))
        {
            if (!handle->has_connection) break;

            /* Unlock before I/O; the network thread must not be blocked
             * by the game thread holding the mutex while sending.         */
            pthread_mutex_unlock(&handle->sync_mutex);
            netplay_send_data(handle);          /* retransmit              */
            pthread_mutex_lock(&handle->sync_mutex);

            if (IS_SYNCED(handle) || !handle->has_connection) break;

            struct timespec wake;
            clock_gettime(CLOCK_REALTIME, &wake);
            /* Fast burst: retransmit 3x at 8ms before falling back to 24ms.
             * Reduces stutter when the bottleneck is our own lost packet.   */
            long iter_ms = (inner_iter < 3) ? 8 : RETRANSMIT_MS;
            inner_iter++;
            wake.tv_nsec += iter_ms * 1000000LL;
            if (wake.tv_nsec >= 1000000000LL) {
                wake.tv_sec++;
                wake.tv_nsec -= 1000000000LL;
            }
            /* Clamp to overall deadline so we never overshoot.           */
            if (wake.tv_sec > deadline.tv_sec ||
                (wake.tv_sec == deadline.tv_sec &&
                 wake.tv_nsec > deadline.tv_nsec)) {
                wake = deadline;
            }

            /* Sleep until the network thread signals us or wake expires.  */
            pthread_cond_timedwait(&handle->sync_cond, &handle->sync_mutex, &wake);

            /* Peer-paused timeout extension:
             * If the peer pauses the game, we extend the deadline indefinitely
             * as long as we keep receiving network activity (silence < 10s).
             * If the network dies while paused, the silence check forces the
             * deadline to expire, preventing an infinite hang. */
            if (handle->is_peer_paused) {
                uint32_t now_ms = netplay_get_ticks_ms();
                uint32_t silence_ms = now_ms - handle->last_recv_time_ms;

                if (!warn_paused) {
                    warn_paused = 1;
                    pthread_mutex_unlock(&handle->sync_mutex);
                    if (handle->netplay_warn)
                        handle->netplay_warn((char*)"TOAST:@peer_paused");
                    myosd_droid_netplay_set_exitPause(1);
                    pthread_mutex_lock(&handle->sync_mutex);
                }

                if (silence_ms < 10000) {
                    /* Peer is paused but still reachable: extend deadline and keep waiting. */
                    clock_gettime(CLOCK_REALTIME, &deadline);
                    deadline.tv_sec  += SYNC_TIMEOUT_MS / 1000;
                    deadline.tv_nsec += (SYNC_TIMEOUT_MS % 1000) * 1000000LL;
                    if (deadline.tv_nsec >= 1000000000LL) {
                        deadline.tv_sec++;
                        deadline.tv_nsec -= 1000000000LL;
                    }
                    continue;  /* continue ONLY when network is alive */
                }
                /* else: silence >= 10s, network is dead.
                 * Do NOT continue. Fall through to the deadline check below. */
            }

            /* Check whether the overall deadline has expired.            */
            struct timespec now_ts;
            clock_gettime(CLOCK_REALTIME, &now_ts);
            if (now_ts.tv_sec > deadline.tv_sec ||
                (now_ts.tv_sec == deadline.tv_sec &&
                 now_ts.tv_nsec >= deadline.tv_nsec)) {
                NLOG("SYNC TIMEOUT: frame=%u target=%u peer=%u peer_peer=%u",
                     handle->frame, handle->target_frame,
                     handle->peer_frame, handle->peer_peer_frame);
                break;
            }
        }
    }
    else
    {
        handle->timeout_cnt = 0;
    }

    /* ── Post-wait: check final state ──────────────────────────────────── */

    if (!IS_SYNCED(handle))
    {
        /* Still not synced after the timeout – declare hangup.           */
        pthread_mutex_unlock(&handle->sync_mutex);
        if (handle->has_connection) {
            handle->has_connection = 0;
            netplay_warn_hangup(handle);
        }
        return;
    }

    /* Commit the peer state and our own snapshot for this frame.
     * Both accesses are under the mutex so no torn reads.                */
    if (handle->frame == handle->target_frame)
    {
        handle->state      = handle->state_tmp;
        handle->peer_state = handle->peer_state_tmp;
    }

    pthread_mutex_unlock(&handle->sync_mutex);

    /* ── Auto-frameskip RTT check (host only, every 5 s) ──────────────── */
    if (handle->player1) {
        uint32_t now = netplay_get_ticks_ms();
        if (now - handle->rtt_update_time > 5000) {

            /* Snapshot RTT accumulators and reset the peak/min counter.      */
            pthread_mutex_lock(&handle->sync_mutex);
            uint32_t frtt = handle->fast_rtt;
            uint32_t srtt = handle->smoothed_rtt;
            uint32_t mrtt = handle->max_rtt_interval;
            uint32_t minrtt = handle->min_rtt_window;
            if (minrtt == 0) minrtt = srtt; /* fallback if no packets received */
            
            uint32_t n1_count = handle->recovery_n1_count;
            handle->recovery_n1_count = 0;
            pthread_mutex_unlock(&handle->sync_mutex);

            /* Compromise Jitter Buffer: baseline = min RTT + 50% of the jitter
             * envelope (peak-min), balancing lag vs stuttering.                */
            uint32_t jitter_envelope = (mrtt > minrtt) ? (mrtt - minrtt) : 0;
            uint32_t rtt_to_use = minrtt + (jitter_envelope / 2);
            
            NLOG("Ping Trace: Min=%u Fast=%u Peak=%u FS=%u N1_recv=%u",
                 minrtt, frtt, mrtt, handle->frame_skip, n1_count);

            if (rtt_to_use > 0 && handle->is_auto_frameskip) {

                uint32_t our_lag = handle->target_frame - handle->peer_peer_frame;
                if (our_lag > handle->frame_skip * 2) {
                    NLOG("WARN: Asymmetric loss detected - peer has only ACKed up to frame %u "
                         "(our target=%u, lag=%u frames)", 
                         handle->peer_peer_frame, handle->target_frame, our_lag);
                }

                pthread_mutex_lock(&handle->sync_mutex);
                int epoch_pending = (handle->frameskip_epoch_frame != 0);
                pthread_mutex_unlock(&handle->sync_mutex);

                if (!epoch_pending) {
                    uint32_t optimal = rtt_to_use / 16;
                    
                    /* Loss compensation: packet loss is independent of RTT and requires
                     * extra buffer depth. Add frames proportional to recovery frequency. */
                    if      (n1_count >= 5)  optimal += 2;   /* heavy loss rate: add 2       */
                    else if (n1_count >= 2)  optimal += 1;   /* moderate loss: add 1         */

                    if (optimal < 2)  optimal = 2;
                    if (optimal > 10) optimal = 10;
                    if (optimal % 2 != 0) optimal++;

                    NLOG("Auto-Frameskip: rtt_to_use=%u optimal=%u current=%u",
                         rtt_to_use, optimal, handle->frame_skip);

                    if (handle->frame_skip != optimal) {
                        uint32_t offset = (optimal > handle->frame_skip) ? 30 : 120;
                        pthread_mutex_lock(&handle->sync_mutex);
                        uint32_t nf = handle->target_frame + offset;
                        /* Monotonic epoch id + own-echo suppression: same
                         * discipline as the manual scheduler */
                        if (nf <= handle->last_epoch_received)
                            nf = handle->last_epoch_received + 1;
                        handle->frameskip_epoch_frame = nf;
                        handle->frameskip_epoch_value = (uint8_t)optimal;
                        /* Epoch also carries the auto flag, or consuming it
                         * would flip both peers to Fixed.                    */
                        handle->frameskip_epoch_is_auto = 1;
                        handle->last_epoch_received = nf;
                        pthread_mutex_unlock(&handle->sync_mutex);
                        NLOG("Auto-Frameskip: scheduled epoch frameskip=%u at frame=%u",
                             optimal, nf);
                    }
                }
            }
            handle->rtt_update_time = now;
        }
    }
}

/* Telemetry: correlate host vs client progress, logged once per second of
 * emulated frames from the game thread.                                     */
static void netplay_log_frame_telemetry(netplay_t *handle)
{
    if ((handle->frame % 60) != 0) return;
    NLOG("TELEM f=%u tf=%u pf=%u pp=%u fs=%u wc=%u role=%s rb=%d",
         handle->frame, handle->target_frame, handle->peer_frame,
         handle->peer_peer_frame, handle->frame_skip,
         netplay_get_ticks_ms(), handle->player1 ? "HOST" : "CLIENT",
         handle->requires_rollback);
}

/* Desync pinpoint diagnostic: dump the applied input ring for the last ~120
 * frames.  A synced session must mirror HOST.loc[f]==CLIENT.peer[f] and vice
 * versa; the first frame where that breaks is the exact diverging frame.    */
static void netplay_log_input_ring(netplay_t *handle)
{
    uint32_t cur = handle->frame;
    uint32_t span = ROLLBACK_RING_FRAMES - 1; /* most we can trust in the ring */
    uint32_t start = (cur > span) ? (cur - span) : 1;
    NLOG("IRING_DUMP frame=%u role=%s span=%u..%u",
         cur, handle->player1 ? "HOST" : "CLIENT", start, cur);
    pthread_mutex_lock(&handle->sync_mutex);
    for (uint32_t f = start; f <= cur; f++) {
        int idx = (int)(f % ROLLBACK_RING_FRAMES);
        netplay_frame_history_t *h = &handle->frame_history[idx];
        if (h->frame != f) continue; /* slot already recycled for another frame */
        NLOG("IRING f=%u loc=0x%08x peer=0x%08x applied=0x%08x conf=%d",
             f, h->local_state.digital, h->peer_state.digital,
             h->applied_peer_state.digital, h->peer_confirmed);
    }
    pthread_mutex_unlock(&handle->sync_mutex);
}

/* Called just after MAME executes a frame: sample local input, schedule/
 * send it, and advance the frame counters (see file header for both modes). */
void netplay_post_frame_net(netplay_t *handle)
{
    if (!handle->has_connection || !handle->has_begun_game) return;

    netplay_log_frame_telemetry(handle);

    /* Connection-quality overlay: pushed to Java every ~2s over the same
     * netplay_warn channel as TOAST/TOASTERR (STATS: prefix).  Java gates
     * whether it's drawn.  Skipped until smoothed_rtt has a first sample,
     * so we never show a misleading 0ms right away.                       */
    {
        static uint32_t s_last_stats_ms = 0;
        uint32_t now_ms = netplay_get_ticks_ms();
        if (handle->netplay_warn && handle->smoothed_rtt > 0 &&
            (now_ms - s_last_stats_ms) > 2000) {
            s_last_stats_ms = now_ms;

            /* Jitter = RTT mean deviation (RFC 6298 mdev), updated on BOTH
             * receive paths so rollback shows real jitter, not the flat 0 the
             * lockstep-only peak/floor envelope gave.  Unlocked read is benign. */
            uint32_t jitter = handle->rtt_mdev;

            /* Colour = WORSE of ping and jitter.  Tuned for INTERNET rollback:
             * rollback hides latency so ping is judged leniently; jitter is the
             * real enemy, so a scattery link reddens even when the ping is fine. */
            int sev = 0;                                /* 0 green,1 yellow,2 red */
            if (handle->smoothed_rtt > 180)      sev = 2;   /* sluggish even w/ rollback */
            else if (handle->smoothed_rtt > 100) sev = 1;   /* good, slightly laggy      */

            /* Jitter rating with HYSTERESIS (worsen at high edge, recover past a
             * lower one) so it doesn't flip-flop: green<->yellow 30/20, yellow<->
             * red 60/45 mdev-ms.  Normal internet/VPN (~10-15ms) sits green.     */
            static int s_jsev = 0;
            int jsev = s_jsev;
            switch (s_jsev) {
                case 0:  if (jitter > 30) jsev = 1; break;
                case 1:  if (jitter > 60) jsev = 2; else if (jitter < 20) jsev = 0; break;
                default: if (jitter < 45) jsev = 1; break;      /* case 2 (red)   */
            }
            s_jsev = jsev;
            if (jsev > sev) sev = jsev;
            const char *quality = (sev == 2) ? "RED" : (sev == 1) ? "YELLOW" : "GREEN";

            char buf[128];
            snprintf(buf, sizeof(buf), "STATS:%s:%s | Ping %ums | Jitter %ums | Delay %uf%s",
                     quality,
                     handle->mode == NETPLAY_MODE_ROLLBACK ? "Rollback" : "Lockstep",
                     (unsigned)handle->smoothed_rtt,
                     (unsigned)jitter,
                     (unsigned)((handle->frame_skip + 1) / 2),
                     handle->is_auto_frameskip ? " (Auto)" : "");
            handle->netplay_warn(buf);
        }
    }

    /* Disabled by default -- netplay_log_input_ring holds sync_mutex for up
     * to ROLLBACK_MAX_FRAMES log calls, stalling packet processing every
     * ~2s.  Enable only when diagnosing a cross-peer input-pairing bug.       */
    constexpr bool IRING_DUMP_ENABLED = false;
    if (IRING_DUMP_ENABLED &&
        handle->mode == NETPLAY_MODE_ROLLBACK && handle->rollback_enabled &&
        handle->frame > 0 && (handle->frame % 120) == 0) {
        netplay_log_input_ring(handle);
    }

    /* ── ROLLBACK MODE ──────────────────────────────────────────────────── *
     * frame_skip in rollback mode acts as Input Delay (GGPO style).         *
     * We read the local input NOW, send the packet, and schedule it for     *
     * target_frame = frame + frame_skip.                                    */
    if (handle->mode == NETPLAY_MODE_ROLLBACK) {
        if (!handle->rollback_enabled) { handle->frame++; return; }

        /* Atomic (target_frame, input) publication: advancing target_frame
         * and publishing state_tmp/the ring slot in separate critical
         * sections would let the ACK path broadcast a MISPAIRED (frame,
         * input) in the gap -> silent permanent desync.  Read input FIRST,
         * publish everything under ONE lock.                                 */
        netplay_state_t ns = handle->state_tmp; /* game-thread-owned, safe unlocked read */

        /* Read local input for this frame (to be sent to peer).          */
        if (handle->frame != 0) {
            ns.digital      = myosd_droid_netplay_joystick_read(0);
            ns.analog_x     = myosd_droid_netplay_joystick_read_analog(0, 'x');
            ns.analog_y     = myosd_droid_netplay_joystick_read_analog(0, 'y');
            ns.analog_rx    = myosd_droid_netplay_joystick_read_analog(0, 'X');
            ns.analog_ry    = myosd_droid_netplay_joystick_read_analog(0, 'Y');
            ns.analog_lz    = myosd_droid_netplay_joystick_read_analog(0, 'l');
            ns.analog_rz    = myosd_droid_netplay_joystick_read_analog(0, 'r');
            ns.mouse_status = myosd_droid_netplay_mouse_read(0);
            handle->local_abs_mouse_x     += myosd_droid_netplay_mouse_read_analog(0, 'x');
            handle->local_abs_mouse_y     += myosd_droid_netplay_mouse_read_analog(0, 'y');
            ns.mouse_x      = handle->local_abs_mouse_x;
            ns.mouse_y      = handle->local_abs_mouse_y;
            ns.lightgun_x   = myosd_droid_netplay_lightgun_read_analog(0, 'x');
            ns.lightgun_y   = myosd_droid_netplay_lightgun_read_analog(0, 'y');
            ns.ext          = myosd_droid_netplay_get_ext_status();
        }

        pthread_mutex_lock(&handle->sync_mutex);

        /* Apply a scheduled mid-game FIXED delay change (Host's Settings ->
         * netplay_ui_set_delay's mid-game epoch branch), if its frame has
         * arrived.  Rollback has its own path here since it returns before
         * ever reaching the LOCKSTEP copy of this logic below.              */
        if (handle->frameskip_epoch_frame > 0 &&
            handle->frame >= handle->frameskip_epoch_frame) {
            handle->frame_skip            = handle->frameskip_epoch_value;
            handle->is_auto_frameskip     = handle->frameskip_epoch_is_auto;
            handle->frameskip_epoch_frame = 0;
            NLOG("Applied Epoch frameskip (rollback): %u auto=%u", handle->frame_skip, handle->is_auto_frameskip);
        }

        /* Adaptive input delay (see NETPLAY_INPUT_DELAY_* in netplay.h).
         * Auto mode, HOST-AUTHORITATIVE: only the host (player1) evaluates
         * the RTT and schedules the change through the epoch mechanism, so
         * both peers switch to the same value on the same frame instead of
         * each side computing (and drifting to) its own.                    */
        if (handle->player1 && handle->is_auto_frameskip &&
            handle->frameskip_epoch_frame == 0 && handle->frame > 120 &&
            handle->frame - handle->fs_adjust_last_frame >= 120) {
            handle->fs_adjust_last_frame = handle->frame;
            uint32_t desired = ((handle->smoothed_rtt / 2) + 15) / 16 + 1; /* half-RTT in frames (ceil) + 1 margin */
            if (desired < NETPLAY_INPUT_DELAY_MIN) desired = NETPLAY_INPUT_DELAY_MIN;
            if (desired > NETPLAY_INPUT_DELAY_MAX) desired = NETPLAY_INPUT_DELAY_MAX;
            if (desired != handle->frame_skip) {
                uint32_t step   = (desired > handle->frame_skip)
                                      ? handle->frame_skip + 1 : handle->frame_skip - 1;
                uint32_t offset = (step > handle->frame_skip) ? 30 : 120;
                uint32_t nf     = handle->target_frame + offset;
                /* Monotonic epoch id + own-echo suppression: same discipline
                 * as the manual scheduler, see netplay_ui_set_delay.        */
                if (nf <= handle->last_epoch_received)
                    nf = handle->last_epoch_received + 1;
                handle->frameskip_epoch_frame   = nf;
                handle->frameskip_epoch_value   = (uint8_t)step;
                handle->frameskip_epoch_is_auto = 1; /* stays in Auto */
                handle->last_epoch_received     = nf;
                NLOG("Auto-Frameskip (rollback host): epoch fs=%u at frame=%u rtt=%u",
                     step, nf, handle->smoothed_rtt);
            }
        }

        /* Update N-1 history BEFORE advancing the target frame.
         * Must perfectly match the unmodified state sent to the peer. */
        handle->prev_target_frame = handle->target_frame;
        handle->prev_state_sent   = handle->state_tmp;

        /* Advance target_frame and publish input in the same critical section
         * as above.  With a dynamic delay the step isn't always +1 -- grew:
         * fill every slot up to new_target; shrank: HOLD for one frame
         * instead of re-committing an already-sent slot.                     */
        uint32_t prev_target = handle->target_frame;
        uint32_t new_target  = handle->frame + handle->frame_skip;
        if (new_target > prev_target) {
            handle->target_frame = new_target;
            handle->state_tmp    = ns;
            for (uint32_t t = prev_target + 1; t <= new_target; t++)
                handle->frame_history[t % ROLLBACK_RING_FRAMES].local_state = ns;
        }
        /* else: hold (delay shrink) — target/state_tmp/ring stay untouched. */

        /* Expose the local state for the CURRENT frame which is
         * what apply_netplay_input_state will inject into MAME! */
        int cur_idx = (int)(handle->frame % ROLLBACK_RING_FRAMES);
        handle->state = handle->frame_history[cur_idx].local_state;

        handle->frame++;
        handle->peer_frame = handle->target_frame; /* rollback uses target_frame for UDP sync */

        pthread_mutex_unlock(&handle->sync_mutex);

        /* Send this frame's input (and our confirmed-watermark CRC) to the peer. */
        if (!netplay_send_data(handle)) {
            handle->has_connection = 0;
            netplay_warn_hangup(handle);
        }
        return;
    }
    /* ── END ROLLBACK MODE ──────────────────────────────────────────────── */

    if (handle->frame == handle->target_frame)
    {
        /* Same atomic-publication discipline as the rollback branch above --
         * in lockstep there's no rollback to heal a mispairing, so it would
         * be an instant permanent desync.                                    */
        netplay_state_t ns = handle->state_tmp;
        if (handle->frame != 0)
        {
            ns.digital     = myosd_droid_netplay_joystick_read(0);
            ns.analog_x    = myosd_droid_netplay_joystick_read_analog(0, 'x');
            ns.analog_y    = myosd_droid_netplay_joystick_read_analog(0, 'y');
            ns.analog_rx   = myosd_droid_netplay_joystick_read_analog(0, 'X');
            ns.analog_ry   = myosd_droid_netplay_joystick_read_analog(0, 'Y');
            ns.analog_lz   = myosd_droid_netplay_joystick_read_analog(0, 'l');
            ns.analog_rz   = myosd_droid_netplay_joystick_read_analog(0, 'r');
            ns.mouse_status = myosd_droid_netplay_mouse_read(0);
            handle->local_abs_mouse_x    += myosd_droid_netplay_mouse_read_analog(0, 'x');
            handle->local_abs_mouse_y    += myosd_droid_netplay_mouse_read_analog(0, 'y');
            ns.mouse_x     = handle->local_abs_mouse_x;
            ns.mouse_y     = handle->local_abs_mouse_y;
            ns.lightgun_x  = myosd_droid_netplay_lightgun_read_analog(0, 'x');
            ns.lightgun_y  = myosd_droid_netplay_lightgun_read_analog(0, 'y');
            ns.ext         = myosd_droid_netplay_get_ext_status();
        }

        pthread_mutex_lock(&handle->sync_mutex);

        handle->timeout_cnt = 0;

        /* Apply epoch if the scheduled frame has arrived.                */
        if (handle->frameskip_epoch_frame > 0 &&
            handle->frame >= handle->frameskip_epoch_frame) {
            handle->frame_skip            = handle->frameskip_epoch_value;
            handle->is_auto_frameskip     = handle->frameskip_epoch_is_auto;
            handle->frameskip_epoch_frame = 0;

            /* Invalidate the pre-fetch: it was calculated with the previous
             * frame_skip offset, now stale.                                */
            handle->peer_next_frame       = 0;
            
            NLOG("Applied Epoch frameskip: %u (pre-fetch invalidated)", handle->frame_skip);
        }

        /* Update N-1 history BEFORE advancing the target frame.
         * Must perfectly match the unmodified state sent to the peer. */
        handle->prev_target_frame = handle->target_frame;
        handle->prev_state_sent   = handle->state_tmp;

        /* Advance the target frame.                                      */
        uint32_t new_target    = handle->frame + handle->frame_skip;
        handle->target_frame   = new_target;

        /* Promote the pre-fetched peer frame if it matches the new target.*/
        if (new_target == handle->peer_next_frame) {
            handle->peer_frame     = handle->peer_next_frame;
            handle->peer_state_tmp = handle->peer_next_state_tmp;
            handle->peer_next_frame= 0;
            /* Wake pre_frame_net if it already entered the wait loop.    */
            pthread_cond_signal(&handle->sync_cond);
        }

        /* Publish the new target's input in the SAME critical section that
         * advanced target_frame (input was read above).                   */
        handle->state_tmp = ns;

        pthread_mutex_unlock(&handle->sync_mutex);

        /* netplay_send_data also updates prev_target_frame / prev_state_sent. */
        if (!netplay_send_data(handle))
        {
            handle->has_connection = 0;
            netplay_warn_hangup(handle);
            return;
        }
    }

    handle->frame++;
}

/* ============================================================
 * SECTION 3 -- Network receive path
 * netplay_read_data() is the network thread's entry point, called in a
 * loop for every incoming packet.  Forward declarations below: the two
 * warning/resync functions it needs are defined later, in section 5
 * (session lifecycle) -- kept there since they are session-lifecycle
 * concerns, not receive-path logic.
 * ============================================================ */

int  netplay_resync_begin(netplay_t *handle, const char *origin);
void netplay_warn_disconnect(netplay_t *handle);

/* Refuse mixed builds at handshake time (see NETPLAY_PROTOCOL_VERSION in
 * netplay.h).  Called on JOIN (host side) and JOIN_ACK (client side) BEFORE
 * any session state is adopted.  Returns 1 if compatible; on mismatch warns
 * the user, notifies the peer and drops the connection.                      */
static int netplay_check_build_compat(netplay_t *handle, const netplay_msg_t *msg, const char *ctx)
{
    uint32_t peer_proto  = ntohl(msg->u.join.protocol);
    uint32_t peer_limit  = ntohl(msg->u.join.state_limit);
    uint32_t peer_maxf   = ntohl(msg->u.join.max_frames);
    uint32_t peer_budget = ntohl(msg->u.join.ring_budget);
    if (peer_proto  == (uint32_t)NETPLAY_PROTOCOL_VERSION &&
        peer_limit  == (uint32_t)ROLLBACK_STATE_SIZE_LIMIT &&
        peer_maxf   == (uint32_t)ROLLBACK_MAX_FRAMES &&
        peer_budget == (uint32_t)ROLLBACK_RING_RAM_BUDGET)
        return 1;

    NLOG("%s: INCOMPATIBLE BUILDS - local proto=%u limit=%u maxf=%u budget=%u vs peer proto=%u limit=%u maxf=%u budget=%u",
         ctx, (unsigned)NETPLAY_PROTOCOL_VERSION, (unsigned)ROLLBACK_STATE_SIZE_LIMIT,
         (unsigned)ROLLBACK_MAX_FRAMES, (unsigned)ROLLBACK_RING_RAM_BUDGET,
         peer_proto, peer_limit, peer_maxf, peer_budget);
    /* Rate-limit the toast: the incompatible client retries JOIN at high
     * frequency until our DISCONNECT reaches it, and each retry lands here. */
    {
        static uint32_t s_last_warn_ms = 0;
        uint32_t now_ms = netplay_get_ticks_ms();
        if (handle->netplay_warn && (now_ms - s_last_warn_ms) > 3000) {
            s_last_warn_ms = now_ms;
            char buf[64];
            /* Java localizes "@key|args"; args are the two protocol versions. */
            snprintf(buf, sizeof(buf), "TOAST:@incompatible|%u|%u",
                     (unsigned)NETPLAY_PROTOCOL_VERSION, peer_proto);
            handle->netplay_warn(buf);
        }
    }
    netplay_send_disconnect(handle);
    /* CLIENT aborts its whole join attempt; HOST refuses only THIS join and
     * keeps listening (dropping has_connection would zombify the session).  */
    if (!handle->player1)
        handle->has_connection = 0;
    return 0;
}

/* Compare two netplay_state_t objects (0 = identical).  EXACT bitwise
 * comparison on purpose -- any tolerance/epsilon is a guaranteed silent
 * desync the moment predicted and confirmed values differ below it.  `ext`
 * is excluded (never applied to emulation).                               */
static int netplay_state_differs(const netplay_state_t *a, const netplay_state_t *b)
{
    return (a->digital      != b->digital      ||
            a->analog_x     != b->analog_x     ||
            a->analog_y     != b->analog_y     ||
            a->analog_rx    != b->analog_rx    ||
            a->analog_ry    != b->analog_ry    ||
            a->analog_lz    != b->analog_lz    ||
            a->analog_rz    != b->analog_rz    ||
            a->mouse_x      != b->mouse_x      ||
            a->mouse_y      != b->mouse_y      ||
            a->mouse_status != b->mouse_status ||
            a->lightgun_x   != b->lightgun_x   ||
            a->lightgun_y   != b->lightgun_y);
}

/* Advance and return the "confirmed watermark": the highest
 * frame W whose CRC is FINAL (never alterable by a future rollback), so
 * comparing CRC(W) across peers is an honest desync test.  Monotonic store,
 * capped below a pending rollback target.  Caller MUST hold sync_mutex.    */
static uint32_t netplay_confirmed_watermark(netplay_t *handle)
{
    if (handle->frame == 0)
        return 0;
    uint32_t ceil_exec = handle->frame - 1;   /* last executed+captured frame */

    uint32_t W = handle->confirmed_watermark;
    while (W + 1 <= ceil_exec) {
        int idx = (int)((W + 1) % ROLLBACK_RING_FRAMES);
        if (handle->frame_history[idx].frame == (W + 1) &&
            handle->frame_history[idx].peer_confirmed)
            W++;
        else
            break;
    }
    handle->confirmed_watermark = W;           /* monotonic store */

    uint32_t eff = W;
    if (handle->requires_rollback && handle->rollback_to_frame > 0 &&
        handle->rollback_to_frame - 1 < eff)
        eff = handle->rollback_to_frame - 1;   /* stale CRCs >= R: cap below R */
    return eff;
}

/* Debug one-shot: HOST ships its per-save_item CRC table for `frame` to the
 * CLIENT (fragmented).  Defined below; declared here for netplay_read_data. */
static void netplay_send_itemcrc_table(netplay_t *handle, uint32_t frame);

/* CLIENT-side snapshot of its OWN per-item CRC table, captured at the moment the
 * desync is detected (frame still fresh in the ring).  Diffed against the HOST's
 * table once it finishes arriving — by then the ring may have evicted the frame,
 * so we must NOT recompute from the ring.  Network-thread-only (single reader). */
static uint32_t g_client_itemcrc_snap[65536];
static uint32_t g_client_itemcrc_count = 0;
static uint32_t g_client_itemcrc_frame = 0xFFFFFFFF;

/* Read and process one incoming packet (network thread, non-blocking).
 * Returns 0 on a transport read failure, 1 otherwise.                     */
int netplay_read_data(netplay_t *handle)
{
    netplay_msg_t msg;

    /* Transport contract: 0 = fatal error, 1 = valid packet, 2 = foreign
     * datagram silently dropped (no session state touched).               */
    int r = handle->read_pkt_data(handle, &msg);
    if (r == 0)
        return 0;
    if (r == 2)
        return 1;

    uint32_t msg_packet_uid = ntohl(msg.packetid);
    msg.msg_type = ntohl(msg.msg_type);

    /* Strict packet drop (N-1 lockstep/rollback correctness): an older DATA
     * packet is redundant once N+1 arrived, so drop it -- but ONLY for DATA;
     * fragmented transfers like STATE_CHUNK need every packet.             */
    if (msg.msg_type == NETPLAY_MSG_DATA) {
        if (msg_packet_uid <= handle->recv_packet_uid) {
            return 1;
        }
        handle->recv_packet_uid = msg_packet_uid;
    }

    handle->last_recv_time_ms = netplay_get_ticks_ms();

    switch (msg.msg_type) {

    case NETPLAY_MSG_DATA:
    {
        if (!handle->has_begun_game)
            break;

        /* While a resync episode is in flight, DATA is POISON -- stale
         * pre-reset packets could (re)confirm old-timeline inputs the heal
         * path can never see.  Drop all of it.                             */
        if (handle->resync_active)
            break;

        uint32_t msg_timestamp  = ntohl(msg.u.data.timestamp);
        uint32_t msg_echo       = ntohl(msg.u.data.echo_timestamp);
        uint32_t peer_frame     = ntohl(msg.u.data.peer_frame);
        uint32_t peer_frame_prev= ntohl(msg.u.data.peer_frame_prev);

        /* Rollback mode never blocks: just update RTT accumulators and check
         * our prediction against the peer's real input.  We still send an
         * ACK (netplay_send_data) so the peer gets RTT.                     */
        if (handle->mode == NETPLAY_MODE_ROLLBACK && handle->rollback_enabled) {

            /* RTT update under mutex (reuse lockstep path below).         */
            pthread_mutex_lock(&handle->sync_mutex);

            if (msg_timestamp != 0)
                handle->last_peer_timestamp = msg_timestamp;

            if (msg_echo != 0) {
                uint32_t rtt = netplay_get_ticks_ms() - msg_echo;
                if (rtt < 2000) {
                    handle->fast_rtt = (handle->fast_rtt == 0)
                        ? rtt : (handle->fast_rtt * 3 + rtt) / 4;
                    /* Jitter = EMA of |rtt - smoothed| (mdev); measured HERE too
                     * so the overlay shows real jitter during rollback play, not
                     * the flat 0 the lockstep-only peak/floor envelope gave.     */
                    if (handle->smoothed_rtt != 0) {
                        uint32_t dev = (rtt > handle->smoothed_rtt)
                            ? rtt - handle->smoothed_rtt : handle->smoothed_rtt - rtt;
                        handle->rtt_mdev = (handle->rtt_mdev * 3 + dev) / 4;
                    }
                    handle->smoothed_rtt = (handle->smoothed_rtt == 0)
                        ? rtt : (handle->smoothed_rtt * 7 + rtt) / 8;
                }
            }
            handle->is_peer_paused = msg.u.data.is_peer_paused;

            /* Frame-advantage time-sync: remember the peer's latest frame so the
             * game thread can throttle if it runs too far ahead.  peer_frame is
             * the peer's target_frame (= peer's frame + frame_skip).            */
            int ack_worthy = (peer_frame > handle->last_received_peer_frame);
            if (ack_worthy)
                handle->last_received_peer_frame = peer_frame;

            /* Track the peer's ACTUAL input delay from the packet (the
             * adaptive delay is per-side, it can differ from ours), so the
             * frame-advantage stall converts the peer's target_frame into
             * its real executed frame with the right offset.                  */
            if (msg.u.data.peer_frame_skip > 0)
                handle->peer_frame_skip = msg.u.data.peer_frame_skip;

            /* Epoch Handshake: ROLLBACK has its own, separate DATA-receive
             * path from the LOCKSTEP copy further down, so it needs its own
             * mirror of that logic to pick up a peer's scheduled mid-game
             * delay change instead of silently dropping it.                */
            {
                uint32_t peer_epoch_frame   = ntohl(msg.u.data.frameskip_epoch_frame);
                uint8_t  peer_epoch_value   = msg.u.data.frameskip_epoch_value;
                uint8_t  peer_epoch_is_auto = msg.u.data.frameskip_epoch_is_auto;
                if (peer_epoch_frame > handle->last_epoch_received) {
                    handle->last_epoch_received = peer_epoch_frame;
                    uint32_t local_frame = handle->frame;
                    handle->frameskip_epoch_frame =
                        (peer_epoch_frame >= local_frame) ? peer_epoch_frame : local_frame;
                    handle->frameskip_epoch_value   = peer_epoch_value;
                    handle->frameskip_epoch_is_auto = peer_epoch_is_auto;
                    NLOG("Epoch RECEIVED (rollback): frameskip=%u auto=%u at frame=%u (now=%u)",
                         peer_epoch_value, peer_epoch_is_auto, peer_epoch_frame, local_frame);
                }
            }

            /* Honest desync detector: the peer ships CRC(W) at ITS confirmed
             * watermark (state FINAL there); we compare only once OUR
             * watermark reaches W too, so any mismatch is a REAL permanent
             * desync, never transient prediction noise.                        */
            uint32_t msg_checksum  = ntohl(msg.u.data.state_checksum);
            uint32_t msg_chk_frame = ntohl(msg.u.data.checksum_frame);
            uint32_t my_watermark  = netplay_confirmed_watermark(handle);
            if (msg_checksum != 0 && handle->rollback_query_checksum &&
                msg_chk_frame != 0 && my_watermark >= msg_chk_frame &&
                /* Skip while OUR ring state for this frame may still be the
                 * stale pre-correction capture (see crc_dirty, netplay.h).    */
                (!handle->crc_dirty || msg_chk_frame < handle->crc_dirty_low)) {
                uint32_t our_crc = handle->rollback_query_checksum(msg_chk_frame);
                if (our_crc != 0 && our_crc != msg_checksum) {
                    static int desync_print_count = 0;
                    static bool s_item_probe_done = false;
                    static uint32_t s_last_frame = 0;
                    static uint32_t s_last_desync_warn_ms = 0;

                    // HACK: reset statics if frame goes backwards (new session)
                    if (handle->frame < s_last_frame || s_last_frame == 0) {
                        desync_print_count = 0;
                        s_item_probe_done = false;
                        s_last_desync_warn_ms = 0;
                    }
                    s_last_frame = handle->frame;

                    /* A real desync is PERMANENT, so once tripped every later
                     * watermark comparison also differs -- throttle the log
                     * (first handful, then 1 in 30) to keep logcat sane.        */
                    if (desync_print_count < 5 || (desync_print_count % 30) == 0) {
                        NLOG("=== DESYNC DETECTED (confirmed) === frame=%u our_crc=0x%08x peer_crc=0x%08x wm=%u rtt=%u",
                             msg_chk_frame, our_crc, msg_checksum,
                             my_watermark, handle->smoothed_rtt);
                        netplay_applied_ring_arm("DESYNC", msg_chk_frame);
                        if (desync_print_count < 3)
                            myosd_netplay_log_sectional_crc(msg_chk_frame);
                    }
                    /* User-facing warning, rate-limited to 1 per 10s (reset
                     * with the other statics above on a new session): a real
                     * desync re-triggers on EVERY later watermark compare
                     * (see comment above), which without a cooldown would
                     * storm the UI with a toast per compare.  This only ever
                     * runs at all when the CRC detector is compiled/armed
                     * (NETPLAY_CRC_DETECTOR_ENABLED), since msg_checksum is
                     * always 0 otherwise and this whole `if` never becomes
                     * true. */
                    {
                        uint32_t now_ms = netplay_get_ticks_ms();
                        if (handle->netplay_warn && (now_ms - s_last_desync_warn_ms) > 10000) {
                            s_last_desync_warn_ms = now_ms;
                            if (handle->mode == NETPLAY_MODE_ROLLBACK)
                                handle->netplay_warn((char*)"TOASTERR:@desync_rollback");
                            else
                                handle->netplay_warn((char*)"TOASTERR:@desync");
                        }
                    }
                    desync_print_count++;

                    /* Root-cause probe: fires ONCE per process, on the first
                     * detected desync frame F.  HOST ships its per-save_item CRC
                     * table for F; CLIENT diffs and logs only differing items BY
                     * NAME, naming the exact diverging device/field.             */
                    if (!s_item_probe_done) {
                        s_item_probe_done = true;
                        if (handle->player1) {
                            NLOG("ITEM_DIFF: HOST sending per-item CRC table for frame=%u to client", msg_chk_frame);
                            netplay_send_itemcrc_table(handle, msg_chk_frame);
                        } else {
                            /* Snapshot OUR table NOW, while frame F is still in
                             * the ring; the host's table may take >1s (= >60
                             * frames) to arrive, by which point F is evicted.  */
                            g_client_itemcrc_count = myosd_netplay_get_item_crc_table(
                                msg_chk_frame, g_client_itemcrc_snap, 65536);
                            g_client_itemcrc_frame = msg_chk_frame;
                            if (g_client_itemcrc_count == 0)
                                NLOG("ITEM_DIFF: CLIENT slot for frame=%u NOT available at detect time!", msg_chk_frame);
                            else
                                NLOG("ITEM_DIFF: CLIENT snapshotted %u local item CRCs for frame=%u, awaiting host table",
                                     g_client_itemcrc_count, msg_chk_frame);
                        }
                    }

                    /* Deliberately do NOT force a rollback here: inputs up to
                     * this frame are identical on both peers, so re-simulating
                     * would reproduce the same divergence (a determinism bug,
                     * not something rollback can fix).                         */
                }
            }

            /* Verify prediction for peer_frame, peer_frame_prev and the extended
             * 16-frame history, NEWEST -> OLDEST.  newest_mispredict_chk_frame
             * caps each older pass's propagation so it can't clobber a fresher
             * pass's guess.                                                    */
            uint32_t newest_mispredict_chk_frame = 0;
            for (int pass = 0; pass < ROLLBACK_PACKET_HISTORY + 2; pass++) {
                uint32_t chk_frame;
                netplay_state_t real_state;
                
                if (pass == 0) {
                    chk_frame = peer_frame;
                    decode_peer_state(&real_state, &msg.u.data.peer_state_tmp);
                } else if (pass == 1) {
                    chk_frame = peer_frame_prev;
                    if (chk_frame == 0) continue;
                    decode_peer_state(&real_state, &msg.u.data.peer_state_prev);
                } else {
                    int hist_idx = pass - 2;
                    if (peer_frame < (uint32_t)hist_idx) continue;
                    chk_frame = peer_frame - hist_idx;
                    if (chk_frame == 0) continue;
                    decode_peer_state(&real_state, &msg.u.data.rollback_history[hist_idx]);
                }

                /* Only check frames we have stored and haven't confirmed.  */
                int idx = (int)(chk_frame % ROLLBACK_RING_FRAMES);
                
                if (chk_frame > handle->frame || 
                   (chk_frame == handle->frame && handle->frame_history[idx].frame != chk_frame)) {
                    /* Early packet. Store it for when we reach this frame. */
                    int early_idx = (int)(chk_frame % EARLY_BUFFER_SIZE);
                    handle->early_peer_frame[early_idx] = chk_frame;
                    handle->early_peer_state[early_idx] = real_state;
                    if (!handle->has_received_data) {
                        NLOG("ROLLBACK: First peer data received! Waking up initial sync wait.");
                    }
                    handle->has_received_data = 1;
                    pthread_cond_signal(&handle->sync_cond);
                    continue;
                }

                if (handle->frame_history[idx].frame != chk_frame) continue;

                /* Already confirmed?  Normally a redundant duplicate we can drop
                 * -- but a frame can get confirmed WRONG via the early-buffer
                 * path (unvalidated).  Compare instead: a differing value means
                 * we confirmed it wrong earlier -- heal via re-confirm + forced
                 * corrective rollback.                                         */
                if (handle->frame_history[idx].peer_confirmed) {
                    if (netplay_state_differs(&handle->frame_history[idx].peer_state, &real_state)) {
                        NLOG("ROLLBACK confirm-fix frame=%u was_dig=0x%x real_dig=0x%x (wrong early confirm healed)",
                             chk_frame, handle->frame_history[idx].peer_state.digital, real_state.digital);
                        handle->frame_history[idx].peer_state         = real_state;
                        handle->frame_history[idx].applied_peer_state = real_state;
                        netplay_applied_ring_arm("confirm_fix", chk_frame);
                        /* Local ring states >= chk_frame are stale until the
                         * corrective FF re-captures them; mute the CRC
                         * detector for that window (see netplay.h).            */
                        if (!handle->crc_dirty || chk_frame < handle->crc_dirty_low)
                            handle->crc_dirty_low = chk_frame;
                        handle->crc_dirty = 1;
                        if (!handle->requires_rollback ||
                            chk_frame < handle->rollback_to_frame) {
                            handle->rollback_to_frame  = chk_frame;
                            handle->requires_rollback  = 1;
                            handle->rollback_arm_gen = handle->rollback_arm_gen + 1;
                        }
                    }
                    continue;
                }

                /* Mark as confirmed regardless of match / mismatch.       */
                netplay_state_t pred = handle->frame_history[idx].applied_peer_state; /* Use what MAME executed */
                handle->frame_history[idx].peer_state   = real_state;
                handle->frame_history[idx].peer_confirmed = 1;

                /* Trigger rollback on prediction mismatch.                */
                if (netplay_state_differs(&pred, &real_state)) {
                    NLOG("ROLLBACK mismatch frame=%u pred_dig=0x%x real_dig=0x%x",
                         chk_frame, pred.digital, real_state.digital);
                    netplay_applied_ring_arm("ROLLBACK_mismatch", chk_frame);
                    /* Mute the CRC detector for the stale window (see
                     * netplay.h) — states >= chk_frame were executed/captured
                     * with the wrong prediction until the FF re-captures them. */
                    if (!handle->crc_dirty || chk_frame < handle->crc_dirty_low)
                        handle->crc_dirty_low = chk_frame;
                    handle->crc_dirty = 1;
                    /* Always rollback to the oldest mispredicted frame.   */
                    if (!handle->requires_rollback ||
                        chk_frame < handle->rollback_to_frame) {
                        handle->rollback_to_frame  = chk_frame;
                        handle->requires_rollback  = 1;
                        handle->rollback_arm_gen = handle->rollback_arm_gen + 1;
                    }

                    /* Update applied_peer_state immediately for the mispredicted frame. */
                    handle->frame_history[idx].applied_peer_state = real_state;
                    
                    /* Propagate the corrected guess to subsequent unconfirmed
                     * frames up to target_frame (FF clamps handle->frame), capped
                     * below any fresher pass's guess.  applied_peer_state is left
                     * untouched for f <= handle->frame (already executed; do not
                     * falsify history).                                          */
                    uint32_t max_f = (handle->target_frame > handle->frame) ? handle->target_frame : handle->frame;
                    if (newest_mispredict_chk_frame != 0 && newest_mispredict_chk_frame < max_f)
                        max_f = newest_mispredict_chk_frame;
                    for (uint32_t f = chk_frame + 1; f <= max_f; f++) {
                        int f_idx = (int)(f % ROLLBACK_RING_FRAMES);
                        if (!handle->frame_history[f_idx].peer_confirmed) {
                            handle->frame_history[f_idx].peer_state = real_state;
                            if (f > handle->frame)
                                handle->frame_history[f_idx].applied_peer_state = real_state;
                        }
                    }

                    /* Passes run newest -> oldest, so the FIRST mismatch we hit here is
                     * guaranteed to be the newest one; latch it once so any subsequent
                     * (older) pass's propagation caps itself above, per the comment there. */
                    if (newest_mispredict_chk_frame == 0)
                        newest_mispredict_chk_frame = chk_frame;
                }

                if (!handle->has_received_data) {
                    NLOG("ROLLBACK: First peer data received! Waking up initial sync wait.");
                }
                handle->has_received_data = 1;
                pthread_cond_signal(&handle->sync_cond);
            }

            pthread_mutex_unlock(&handle->sync_mutex);

            /* ACK only NEW-frame packets, rate-limited to 8ms -- an
             * unconditional reply turns every DATA packet into an endless
             * ping-pong echo chain, bufferbloating congested links and
             * deepening every rollback.                                       */
            {
                uint32_t now_ms = netplay_get_ticks_ms();
                if (ack_worthy &&
                    (uint32_t)(now_ms - handle->last_ack_send_ms) >= 8) {
                    handle->last_ack_send_ms = now_ms;
                    netplay_send_data(handle);
                }
            }
            break; /* NETPLAY_MSG_DATA rollback done */
        }
        /* ── END ROLLBACK MODE DATA processing ──────────────────────────── */

        /* Lock: all shared peer-state writes go under sync_mutex ──── */
        pthread_mutex_lock(&handle->sync_mutex);

        /* RTT: echo_timestamp is the timestamp we embedded in a previous
         * packet and the peer is echoing back.  RTT = now − that stamp.  */
        if (msg_timestamp != 0)
            handle->last_peer_timestamp = msg_timestamp;

        if (msg_echo != 0) {
            uint32_t rtt = netplay_get_ticks_ms() - msg_echo;
            if (rtt < 2000) {   /* discard absurd spikes                  */
                uint32_t fast_rtt = (handle->fast_rtt == 0)
                    ? rtt
                    : (handle->fast_rtt * 3 + rtt) / 4; /* EMA fast α=1/4 */

                uint32_t slow_rtt = (handle->smoothed_rtt == 0)
                    ? rtt
                    : (handle->smoothed_rtt * 7 + rtt) / 8; /* EMA slow α=1/8 */

                handle->fast_rtt = fast_rtt;
                /* Jitter = EMA of |rtt - smoothed| (mdev), computed before the
                 * smoothed_rtt update so it uses the prior mean (RFC6298).      */
                if (handle->smoothed_rtt != 0) {
                    uint32_t dev = (rtt > handle->smoothed_rtt)
                        ? rtt - handle->smoothed_rtt : handle->smoothed_rtt - rtt;
                    handle->rtt_mdev = (handle->rtt_mdev * 3 + dev) / 4;
                }
                handle->smoothed_rtt = slow_rtt;

                if (handle->max_rtt_interval == 0 || rtt > handle->max_rtt_interval) {
                    handle->max_rtt_interval = rtt;
                } else {
                    /* Decaying Maximum: allows the Peak RTT to slowly track downwards 
                     * over several seconds, but jumps upwards instantly on jitter spikes. 
                     * This creates a stable envelope over the network's maximum latency. */
                    handle->max_rtt_interval = (handle->max_rtt_interval * 63 + rtt) / 64;
                }
                    
                if (handle->min_rtt_window == 0 || rtt < handle->min_rtt_window) {
                    handle->min_rtt_window = rtt;
                } else {
                    /* Decaying Minimum: allows the baseline to slowly track upwards 
                     * if the physical ping genuinely increases, but quickly grabs 
                     * the minimum to filter out jitter spikes. */
                    handle->min_rtt_window = (handle->min_rtt_window * 31 + rtt) / 32;
                }
            }
        }

        handle->is_peer_paused = msg.u.data.is_peer_paused;

        NLOG_VERBOSE("recv peer_frame=%u peer_peer_frame=%u",
                     peer_frame, ntohl(msg.u.data.peer_peer_frame));

        int signaled = 0;
        int state_updated = 0;

        /* Primary path: packet carries exactly the frame we are waiting for. */
        if (handle->target_frame == peer_frame) {
            if (handle->peer_frame != peer_frame) {
                decode_peer_state(&handle->peer_state_tmp, &msg.u.data.peer_state_tmp);
                handle->peer_frame = peer_frame;
                signaled = 1;
                state_updated = 1;
                NLOG_VERBOSE("primary: peer_frame=%u matches target_frame", peer_frame);
            }
        }

        /* N-1 recovery: current packet carries the previous frame
         * as redundancy.  If that previous frame is exactly what we need
         * (i.e. we lost the original packet for it), recover here.        */
        if (!signaled && peer_frame_prev != 0 &&
            handle->target_frame == peer_frame_prev) {
            
            if (handle->peer_frame != peer_frame_prev) {
                decode_peer_state(&handle->peer_state_tmp, &msg.u.data.peer_state_prev);
                handle->peer_frame = peer_frame_prev;
                signaled = 1;
                state_updated = 1;
                handle->recovery_n1_count++;
                NLOG("RECOVERY: recovered frame %u from peer N-1 history", peer_frame_prev);

                /* RECOVERY PREFETCH: When N-1 fires, peer_frame is always the
                 * next sync point in lockstep. Store it immediately,
                 * frame_skip-agnostic, to guarantee no second stutter even during
                 * epoch transitions where local/peer frame_skip may differ briefly. */
                if (peer_frame != 0 && handle->peer_next_frame != peer_frame) {
                    decode_peer_state(&handle->peer_next_state_tmp, &msg.u.data.peer_state_tmp);
                    handle->peer_next_frame = peer_frame;
                    state_updated = 1;
                    NLOG("RECOVERY+PREFETCH: also secured peer_frame=%u for next sync", peer_frame);
                }
            }
        }

        /* Pre-fetch: the peer is one sync-point ahead of what we need.
         * Store it so post_frame_net can promote it instantly.            */
        if (handle->target_frame == handle->peer_frame &&
            handle->target_frame + handle->frame_skip == peer_frame) {
            if (handle->peer_next_frame != peer_frame) {
                decode_peer_state(&handle->peer_next_state_tmp, &msg.u.data.peer_state_tmp);
                handle->peer_next_frame = peer_frame;
                state_updated = 1;
                NLOG_VERBOSE("pre-fetched next peer_frame=%u", peer_frame);
            }
        }

        /* peer_peer_frame: what the peer says it has received from us.   */
        uint32_t peer_peer_frame = ntohl(msg.u.data.peer_peer_frame);
        if (handle->peer_peer_frame < peer_peer_frame)
            handle->peer_peer_frame = peer_peer_frame;

        /* Epoch Handshake:
         * We unconditionally accept epoch frameskip updates as long as they
         * arrive via a strictly newer packet (enforced by recv_packet_uid).
         * This guarantees synchronized frame delays across both peers. */
        uint32_t peer_epoch_frame   = ntohl(msg.u.data.frameskip_epoch_frame);
        uint8_t  peer_epoch_value   = msg.u.data.frameskip_epoch_value;
        uint8_t  peer_epoch_is_auto = msg.u.data.frameskip_epoch_is_auto;
        if (peer_epoch_frame > handle->last_epoch_received) {
            handle->last_epoch_received = peer_epoch_frame;
            uint32_t local_frame = handle->frame; /* benign read of game-thread field */
            if (peer_epoch_frame >= local_frame) {
                /* Normal: epoch is in the future – schedule it.          */
                handle->frameskip_epoch_frame = peer_epoch_frame;
                handle->frameskip_epoch_value = peer_epoch_value;
                handle->frameskip_epoch_is_auto = peer_epoch_is_auto;
                NLOG("Epoch RECEIVED: frameskip=%u auto=%u at frame=%u (now=%u)",
                     peer_epoch_value, peer_epoch_is_auto, peer_epoch_frame, local_frame);
            } else {
                /* Late arrival: epoch frame already passed.
                 * Apply at the next post_frame_net check by scheduling it
                 * for the current frame (>= check will fire immediately). */
                handle->frameskip_epoch_frame = local_frame;
                handle->frameskip_epoch_value = peer_epoch_value;
                handle->frameskip_epoch_is_auto = peer_epoch_is_auto;
                NLOG("Epoch LATE (now=%u, was for %u): applying immediately",
                     local_frame, peer_epoch_frame);
            }
            state_updated = 1;
        }

        /* Signal the game thread if we have what it is waiting for.      */
        if (signaled)
            pthread_cond_signal(&handle->sync_cond);

        pthread_mutex_unlock(&handle->sync_mutex);
        /* ── End lock ─────────────────────────────────────────────────── */

        /* Conditional ACK:
         * We only send an ACK if this packet meaningfully updated our state.
         * This eliminates ACK amplification storms during burst transmission. */
        if (state_updated) {
            /* Send ACK outside the lock to avoid holding mutex during I/O. */
            netplay_send_data(handle);
        }
    }
    break;

    case NETPLAY_MSG_STATE_CHUNK:
    {
        uint32_t chunk_id = ntohl(msg.u.state_chunk.chunk_id);
        uint32_t total_chunks = ntohl(msg.u.state_chunk.total_chunks);
        uint32_t total_size = ntohl(msg.u.state_chunk.total_size);
        uint16_t chunk_size = ntohs(msg.u.state_chunk.chunk_size);
        
        /* First chunk: allocate buffer + bitmap */
        if (!handle->initial_sync_complete && !handle->sync_state_buffer) {
            handle->sync_state_size = total_size;
            handle->sync_total_chunks = total_chunks;
            handle->sync_chunks_received = 0;
            handle->sync_state_buffer = (uint8_t *)malloc(total_size);
            uint32_t bitmap_bytes = (total_chunks + 7) / 8;
            handle->sync_chunk_bitmap = (uint8_t *)calloc(bitmap_bytes, 1);
            /* Frame the host captured this state at (0 = boot-time initial
             * sync).  Adopted by the game thread at injection.             */
            handle->sync_pending_frame = ntohl(msg.u.state_chunk.sync_frame);
        }
        
        /* Accept ANY chunk in ANY order, as long as it's not a duplicate */
        if (!handle->initial_sync_complete && handle->sync_state_buffer && 
            handle->sync_chunk_bitmap && chunk_id < handle->sync_total_chunks) {
            
            uint32_t byte_idx = chunk_id / 8;
            uint8_t  bit_mask = 1 << (chunk_id % 8);
            
            /* Skip duplicates */
            if (!(handle->sync_chunk_bitmap[byte_idx] & bit_mask)) {
                uint32_t offset = chunk_id * STATE_CHUNK_SIZE;
                if (offset + chunk_size <= handle->sync_state_size) {
                    memcpy(handle->sync_state_buffer + offset, msg.u.state_chunk.data, chunk_size);
                    handle->sync_chunk_bitmap[byte_idx] |= bit_mask;
                    __sync_add_and_fetch(&handle->sync_chunks_received, 1);
                    
                    if (handle->sync_chunks_received % 100 == 0 || handle->sync_chunks_received == handle->sync_total_chunks) {
                        NLOG("Client received chunk %d/%d (chunk_id=%d)", 
                             handle->sync_chunks_received, handle->sync_total_chunks, chunk_id);
                    }
                    
                    /* All chunks received! */
                    if (handle->sync_chunks_received >= handle->sync_total_chunks) {
                        NLOG("Client fully received sync state! Uncompressing...");

                        uLongf uncomp_len = ROLLBACK_STATE_SIZE_LIMIT;
                        uint8_t *uncomp_buf = (uint8_t *)malloc(uncomp_len);
                        if (uncomp_buf) {
                            int zerr = uncompress(uncomp_buf, &uncomp_len, handle->sync_state_buffer, handle->sync_state_size);
                            if (zerr == Z_OK) {
                                NLOG("Client successfully uncompressed %u bytes to %lu bytes!",
                                     handle->sync_state_size, uncomp_len);
                                if (handle->sync_pending_buffer) free(handle->sync_pending_buffer);
                                handle->sync_pending_buffer = uncomp_buf;
                                handle->sync_pending_size   = (uint32_t)uncomp_len;
                                __sync_synchronize();
                                handle->sync_state_received = 1;
                                uncomp_buf = NULL;
                            } else {
                                NLOG("Client failed to uncompress state! zerr=%d (comp=%u bytes, buf=%d)",
                                     zerr, handle->sync_state_size, ROLLBACK_STATE_SIZE_LIMIT);
                                if (handle->netplay_warn) {
                                    char warn_buf[64];
                                    snprintf(warn_buf, sizeof(warn_buf),
                                        "TOAST:@state_sync_failed|%.2f",
                                        (double)handle->sync_state_size / (1024.0 * 1024.0));
                                    handle->netplay_warn(warn_buf);
                                }
                                handle->initial_sync_complete = 1;
                            }
                            if (uncomp_buf) free(uncomp_buf);
                        }
                        
                        /* Free bitmap */
                        free(handle->sync_chunk_bitmap);
                        handle->sync_chunk_bitmap = NULL;
                        
                        /* Send final ACK 3 times for UDP redundancy */
                        for (int ack_i = 0; ack_i < 3; ack_i++) {
                            netplay_msg_t ack_msg;
                            memset(&ack_msg, 0, sizeof(ack_msg));
                            ack_msg.packetid = htonl(__sync_add_and_fetch(&handle->packet_uid, 1));
                            ack_msg.msg_type = htonl(NETPLAY_MSG_STATE_ACK);
                            ack_msg.u.state_ack.next_expected_chunk = htonl(handle->sync_total_chunks);
                            handle->send_pkt_data(handle, &ack_msg);
                        }
                    }
                }
            }
        }
    }
    break;

    case NETPLAY_MSG_STATE_ACK:
    {
        if (handle->initial_sync_complete) break;
        uint32_t ack_next = ntohl(msg.u.state_ack.next_expected_chunk);
        
        pthread_mutex_lock(&handle->sync_mutex);
        if (ack_next > handle->sync_last_acked_chunk) {
            handle->sync_last_acked_chunk = ack_next;
        }
        pthread_mutex_unlock(&handle->sync_mutex);
        
        if (ack_next >= handle->sync_total_chunks) {
            NLOG("Host finished sending initial sync state!");
            handle->initial_sync_complete = 1;
            /* Close a resync episode (no-op for the boot sync).            */
            if (handle->resync_active) {
                handle->resync_active = 0;
                handle->resync_last_done_ms = netplay_get_ticks_ms();
                NLOG("RESYNC done (HOST) frame=%u", handle->sync_state_frame);
            }
        } else {
            /* Let the game thread's polling handle the sliding window sending 
             * to avoid ACK storms and blocking the receive thread. */
        }
    }
    break;

    case NETPLAY_MSG_ITEMCRC_CHUNK:
    {
        /* Debug: reassemble the HOST's per-item CRC table for a desynced frame,
         * diff against our own slot, log only differing items.  Per-episode,
         * not one-shot: a NEW desync frame gets processed again.               */
        static uint32_t *dbg_buf    = NULL;
        static uint8_t  *dbg_got    = NULL;   /* per-chunk received flags */
        static uint32_t  dbg_frame  = 0xFFFFFFFF;
        static uint32_t  dbg_total_items  = 0;
        static uint32_t  dbg_total_chunks = 0;
        static uint32_t  dbg_have_chunks  = 0;
        static uint32_t  dbg_last_done_frame = 0xFFFFFFFF;

        uint32_t frame        = ntohl(msg.u.itemcrc_chunk.frame);
        if (frame == dbg_last_done_frame) break;   /* already diffed this frame */
        uint32_t chunk_id     = ntohl(msg.u.itemcrc_chunk.chunk_id);
        uint32_t total_chunks = ntohl(msg.u.itemcrc_chunk.total_chunks);
        uint32_t total_items  = ntohl(msg.u.itemcrc_chunk.total_items);
        uint16_t chunk_items  = ntohs(msg.u.itemcrc_chunk.chunk_items);

        /* (Re)initialise on the first chunk (or if a new frame's table starts). */
        if (dbg_buf == NULL || frame != dbg_frame) {
            if (dbg_buf) { free(dbg_buf); dbg_buf = NULL; }
            if (dbg_got) { free(dbg_got); dbg_got = NULL; }
            if (total_items == 0 || total_items > 65536 || total_chunks == 0) break;
            dbg_buf = (uint32_t *)calloc(total_items, sizeof(uint32_t));
            dbg_got = (uint8_t  *)calloc(total_chunks, 1);
            if (!dbg_buf || !dbg_got) { dbg_last_done_frame = frame; break; }
            dbg_frame        = frame;
            dbg_total_items  = total_items;
            dbg_total_chunks = total_chunks;
            dbg_have_chunks  = 0;
        }

        if (chunk_id < dbg_total_chunks && !dbg_got[chunk_id]) {
            uint32_t base = chunk_id * ITEMCRC_PER_CHUNK;
            for (uint16_t k = 0; k < chunk_items && (base + k) < dbg_total_items; k++)
                dbg_buf[base + k] = ntohl(msg.u.itemcrc_chunk.crcs[k]);
            dbg_got[chunk_id] = 1;
            dbg_have_chunks++;

            if (dbg_have_chunks >= dbg_total_chunks) {
                NLOG("ITEM_DIFF: client received full host table for frame=%u (%u items), diffing...",
                     dbg_frame, dbg_total_items);
                if (dbg_frame == g_client_itemcrc_frame && g_client_itemcrc_count > 0) {
                    /* Eviction-proof path: diff the snapshot we took at detect
                     * time against the host's table (no ring access).          */
                    myosd_netplay_diff_item_crc_tables(dbg_frame,
                        g_client_itemcrc_snap, g_client_itemcrc_count,
                        dbg_buf, dbg_total_items);
                } else {
                    /* Fallback: recompute from the ring (may already be evicted). */
                    NLOG("ITEM_DIFF: no client snapshot for frame=%u (have %u), falling back to ring",
                         dbg_frame, g_client_itemcrc_frame);
                    myosd_netplay_diff_item_crc_table(dbg_frame, dbg_buf, dbg_total_items);
                }
                free(dbg_buf); dbg_buf = NULL;
                free(dbg_got); dbg_got = NULL;
                dbg_last_done_frame = dbg_frame;
            }
        }
    }
    break;

    case NETPLAY_MSG_JOIN:
    {
        NLOG("received NETPLAY_MSG_JOIN: has_begun_game=%d", handle->has_begun_game);
        /* Refuse mixed builds before adopting anything.                   */
        if (!netplay_check_build_compat(handle, &msg, "JOIN"))
            break;
        /* Always answer JOIN with JOIN_ACK.  UDP packets can be dropped,
         * and if the Server starts the game before the Client receives the
         * ACK, we must not ignore the Client's retries.                   */
        handle->has_joined = 1;
        handle->frame = 0;
        handle->target_frame = 0;
        handle->peer_frame = 0;
        memset(handle->frame_history, 0, sizeof(handle->frame_history));

        handle->has_received_data = 0;
        for (int i = 0; i < EARLY_BUFFER_SIZE; i++) {
            handle->early_peer_frame[i] = 0xFFFFFFFF;
        }
        handle->last_crc_match_frame = 0;
        handle->consecutive_desyncs  = 0;
        handle->confirmed_watermark  = 0;
        handle->local_ready = 0;
        handle->peer_ready  = 0;
        handle->last_received_peer_frame = 0;
        handle->sync_state_received = 0;
        if (handle->sync_pending_buffer) {
            free(handle->sync_pending_buffer);
            handle->sync_pending_buffer = NULL;
        }


        NLOG("sending join_ack");
        if (!netplay_send_join_ack(handle)) {
            NLOG("failed to send join_ack");
            return 0;
        }
    }
    break;

    case NETPLAY_MSG_JOIN_ACK:
    {
        /* Refuse mixed builds before adopting anything.                   */
        if (!netplay_check_build_compat(handle, &msg, "JOIN_ACK"))
            break;
        handle->has_joined  = 1;
        handle->frame = 0;
        handle->target_frame = 0;
        handle->peer_frame = 0;
        memset(handle->frame_history, 0, sizeof(handle->frame_history));

        handle->has_received_data = 0;
        for (int i = 0; i < EARLY_BUFFER_SIZE; i++) {
            handle->early_peer_frame[i] = 0xFFFFFFFF;
        }
        handle->last_crc_match_frame = 0;
        handle->consecutive_desyncs  = 0;
        handle->confirmed_watermark  = 0;
        handle->local_ready = 0;
        handle->peer_ready  = 0;
        handle->last_received_peer_frame = 0;

        handle->frame_skip  = msg.u.join.frame_skip;
        handle->mode        = (netplay_mode_type)msg.u.join.mode;
        /* Host is authoritative for Auto/Fixed too, not just the numeric
         * value -- otherwise a Client on its own local Auto pref would keep
         * adjusting frame_skip out from under the Host's fixed choice.     */
        handle->is_auto_frameskip = msg.u.join.is_auto_frameskip;
        NLOG("Set mode to %d from JOIN_ACK", handle->mode);
        handle->basetime    = ntohl(msg.u.join.time);

        /* Adopt the host's MAME samplerate before the game boots -- mixed
         * rates make savestates layout-incompatible.  Session-only, user's
         * local preference untouched.                                        */
        myosd_droid_set_netplay_sound_rate((int)ntohl(msg.u.join.sound_rate));

        /* Config-parity check: if the client had locally selected a different
         * game than the host announces, surface it.  We still adopt the
         * host's game (the host is authoritative), but a mismatch here means
         * the user picked the wrong ROM and would otherwise just see a silent
         * desync.  Driver identity == ROM identity (enforced by media audit). */
        char host_game[MAX_GAME_NAME];
        strncpy(host_game, msg.u.join.game_name, MAX_GAME_NAME - 1);
        host_game[MAX_GAME_NAME - 1] = '\0';
        if (handle->game_name[0] != '\0' &&
            strncmp(handle->game_name, host_game, MAX_GAME_NAME) != 0) {
            NLOG("WARN: game mismatch - local='%s' host='%s'. Adopting host's game.",
                 handle->game_name, host_game);
            if (handle->netplay_warn) {
                char wmsg[128];
                /* arg is the host's game name, which Java inserts into the text. */
                snprintf(wmsg, sizeof(wmsg), "TOAST:@host_running|%s", host_game);
                handle->netplay_warn(wmsg);
            }
        }

        strncpy(handle->game_name, host_game, MAX_GAME_NAME - 1);
        handle->game_name[MAX_GAME_NAME - 1] = '\0';
        NLOG("received join ack for %s with basetime:%s..",
             handle->game_name, ctime(&handle->basetime));
             
        if (handle->mode == NETPLAY_MODE_ROLLBACK) {
            handle->initial_sync_complete = 0;
            handle->sync_state_received = 0;
            if (handle->sync_state_buffer) {
                free(handle->sync_state_buffer);
                handle->sync_state_buffer = NULL;
            }
            if (handle->sync_pending_buffer) {
                free(handle->sync_pending_buffer);
                handle->sync_pending_buffer = NULL;
            }
        }
    }
    break;

    case NETPLAY_MSG_READY:
    {
        /* Symmetric start barrier: the peer is ready for frame 0.  Record it
         * and wake the game thread if it is blocked in the barrier.          */
        if (!handle->peer_ready)
            NLOG("received NETPLAY_MSG_READY from peer");
        handle->peer_ready = 1;
        pthread_mutex_lock(&handle->sync_mutex);
        pthread_cond_broadcast(&handle->sync_cond);
        pthread_mutex_unlock(&handle->sync_mutex);
    }
    break;

    case NETPLAY_MSG_STATE_SIZE:
    {
        /* Record the peer's advertised savestate size.  The game thread's
         * initial-sync block compares it against ours and falls back to
         * lockstep on a mismatch (incompatible save layouts).               */
        uint32_t sz = ntohl(msg.u.state_size.state_size);
        if (sz != 0)
            handle->peer_state_size = sz;
    }
    break;

    case NETPLAY_MSG_RESYNC:
    {
        /* Peer requests a mid-game resync.  begin() is idempotent and
         * self-guarded, so retransmits are harmless; toast only on 0->1.    */
        int was_active = handle->resync_active;
        if (netplay_resync_begin(handle, "peer request") && !was_active) {
            if (handle->netplay_warn)
                handle->netplay_warn((char*)"TOAST:@peer_resync");
        }
    }
    break;

    case NETPLAY_MSG_DISCONNECT:
    {
        NLOG("received disconnect message from peer – terminating connection.");
        netplay_warn_disconnect(handle);
        handle->has_connection = 0;
        /* Wake the game thread if it is sleeping in the sync wait.       */
        pthread_mutex_lock(&handle->sync_mutex);
        pthread_cond_broadcast(&handle->sync_cond);
        pthread_mutex_unlock(&handle->sync_mutex);
    }
    break;

    case NETPLAY_MSG_PUNCH:
        /* NAT hole-punch probe: its only job is the transport-level side
         * effects (opening the sender's NAT mapping, peer latch).          */
        break;

    default:
        NLOG("netplay unknown msg %d", msg.msg_type);
        break;
    }

    return 1;
}

/* ============================================================
 * SECTION 4 -- Network send path
 * The protocol message senders, and netplay_send_data() -- the per-frame
 * DATA packet builder called from section 2's post_frame_net and from
 * section 3's ACK path.
 * ============================================================ */

/* Send a sliding window of the streamed savestate's chunks.               */
int netplay_send_state_chunks(netplay_t *handle)
{
    if (!handle->has_connection || handle->initial_sync_complete || !handle->sync_state_buffer) return 0;
    /* Only the HOST sends state chunks. The CLIENT must never enter here:
     * its recv thread allocates sync_state_buffer upon receiving the first chunk,
     * which would cause this function to compress+send the partial buffer back,
     * corrupting the receive and blocking the game thread. player1==1 = HOST. */
    if (handle->player1 != 1) return 0;
    /* Guard: once we've streamed all chunks, don't re-enter */
    if (handle->sync_total_chunks > 0 && handle->sync_last_acked_chunk >= handle->sync_total_chunks) return 0;
    
    if (!handle->sync_state_compressed) {
        handle->sync_state_compressed = 1;
        uLongf destLen = compressBound(handle->sync_state_size);
        uint8_t *comp_buf = (uint8_t *)malloc(destLen);
        if (comp_buf) {
            int zerr = compress(comp_buf, &destLen, handle->sync_state_buffer, handle->sync_state_size);
            if (zerr == Z_OK) {
                NLOG("Host compressed sync state from %u bytes to %lu bytes!", handle->sync_state_size, destLen);
                handle->sync_state_buffer = comp_buf;
                handle->sync_state_size = (uint32_t)destLen;
                handle->sync_total_chunks = (handle->sync_state_size + STATE_CHUNK_SIZE - 1) / STATE_CHUNK_SIZE;
            } else {
                NLOG("Host failed to compress state! zerr=%d", zerr);
                free(comp_buf);
            }
        }
    }

    /* Two-pass streaming: send ALL chunks twice. The client uses a bitmap so
     * duplicates are free. For a chunk to be lost, it must be dropped in BOTH
     * passes — probability ~0.0001% on uncongested Wi-Fi. Each pass takes ~2s
     * (973 chunks * 2ms). Between passes, check if the client already ACK'd
     * (it might complete on pass 1 alone) and skip pass 2 if so. */
    
    for (int pass = 0; pass < 3; pass++) {
        /* Check if client already confirmed everything */
        if (handle->initial_sync_complete) break;
        
        NLOG("Host: streaming pass %d — %u chunks (%u bytes compressed) to client...", 
             pass + 1, handle->sync_total_chunks, handle->sync_state_size);
        
        for (uint32_t i = 0; i < handle->sync_total_chunks; i++) {
            if (!handle->has_connection || handle->initial_sync_complete) break;
            
            netplay_msg_t msg;
            memset(&msg, 0, sizeof(msg));
            msg.packetid = htonl(__sync_add_and_fetch(&handle->packet_uid, 1));
            msg.msg_type = htonl(NETPLAY_MSG_STATE_CHUNK);
            msg.u.state_chunk.chunk_id = htonl(i);
            msg.u.state_chunk.total_chunks = htonl(handle->sync_total_chunks);
            msg.u.state_chunk.total_size = htonl(handle->sync_state_size);
            /* Capture frame (0 for the boot-time initial sync).            */
            msg.u.state_chunk.sync_frame = htonl(handle->sync_state_frame);
            
            uint32_t offset = i * STATE_CHUNK_SIZE;
            uint32_t remaining = handle->sync_state_size - offset;
            uint16_t csize = (remaining > STATE_CHUNK_SIZE) ? STATE_CHUNK_SIZE : remaining;
            msg.u.state_chunk.chunk_size = htons(csize);
            memcpy(msg.u.state_chunk.data, handle->sync_state_buffer + offset, csize);
            
            if (i % 100 == 0 || i == handle->sync_total_chunks - 1) {
                NLOG("Host sending chunk %d/%d (pass %d)", i, handle->sync_total_chunks, pass + 1);
            }
            
            handle->send_pkt_data(handle, &msg);
            
            /* 2ms pacing per packet = ~500 pkt/s = ~500 KB/s.  Resume on EINTR
             * so a delivered signal can't shorten the sleep.               */
            struct timespec req = {0, 2000000}; // 2ms
            while (nanosleep(&req, &req) == -1 && errno == EINTR);
        }
        
        NLOG("Host: pass %d complete", pass + 1);
    }
    
    /* Prevent re-entry from the polling loop */
    handle->sync_last_acked_chunk = handle->sync_total_chunks;
    
    NLOG("Host: finished streaming all %u chunks (3 passes)", handle->sync_total_chunks);
    return 1;
}

/* Called from myosd-netplay.cpp's sync wait loop: keep the HOST's chunk-send
 * window sliding forward while the game thread waits for the initial sync. */
void myosd_netplay_sync_poll(void) {
    netplay_t *handle = netplay_get_handle();
    if (handle) {
        netplay_send_state_chunks(handle);
    }
}

/* Debug one-shot: ship our per-save_item CRC table for `frame` to the peer,
 * fragmented into NETPLAY_MSG_ITEMCRC_CHUNK packets.  Each chunk is burst-sent a
 * few times since UDP is lossy and this is a one-shot diagnostic (no ACK/retry
 * machinery).  Runs on the network thread, same as the STATE_CHUNK sender.    */
static void netplay_send_itemcrc_table(netplay_t *handle, uint32_t frame)
{
    if (!handle->has_connection) return;

    static uint32_t table[65536];   /* upper bound on save_item count */
    uint32_t count = myosd_netplay_get_item_crc_table(frame, table, 65536);
    if (count == 0) {
        NLOG("ITEM_DIFF: host slot for frame=%u not available, cannot send table", frame);
        return;
    }

    uint32_t total_chunks = (count + ITEMCRC_PER_CHUNK - 1) / ITEMCRC_PER_CHUNK;
    NLOG("ITEM_DIFF: host sending %u item CRCs in %u chunks for frame=%u",
         count, total_chunks, frame);

    for (uint32_t c = 0; c < total_chunks; c++) {
        uint32_t base = c * ITEMCRC_PER_CHUNK;
        uint16_t items = (count - base > ITEMCRC_PER_CHUNK) ? ITEMCRC_PER_CHUNK : (uint16_t)(count - base);

        netplay_msg_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_type = htonl(NETPLAY_MSG_ITEMCRC_CHUNK);
        msg.u.itemcrc_chunk.frame        = htonl(frame);
        msg.u.itemcrc_chunk.chunk_id     = htonl(c);
        msg.u.itemcrc_chunk.total_chunks = htonl(total_chunks);
        msg.u.itemcrc_chunk.total_items  = htonl(count);
        msg.u.itemcrc_chunk.chunk_items  = htons(items);
        for (uint16_t k = 0; k < items; k++)
            msg.u.itemcrc_chunk.crcs[k] = htonl(table[base + k]);

        /* Burst 3x for loss tolerance; packetid must be fresh each send. */
        for (int rep = 0; rep < 3; rep++) {
            msg.packetid = htonl(__sync_add_and_fetch(&handle->packet_uid, 1));
            handle->send_pkt_data(handle, &msg);
        }
    }
}

/* Build and send this frame's DATA packet: local input, N-1/16-frame
 * history, RTT echo, and the confirmed-watermark CRC when due.            */
int netplay_send_data(netplay_t *handle)
{
    netplay_msg_t msg;

    if (!handle->has_connection)
        return 0;

    /* Atomic increment: packet_uid is read from both threads.             */
    uint32_t uid = __sync_add_and_fetch(&handle->packet_uid, 1);
    msg.packetid  = htonl(uid);
    msg.msg_type  = htonl(NETPLAY_MSG_DATA);

    msg.u.data.is_peer_paused = myosd_is_paused();

    /* Audit P4: called from both the game thread and the network thread (ACK
     * path), so every sync-critical field must be read under sync_mutex to
     * avoid torn reads; snapshot once, then build the packet from locals.   */
    uint32_t        snap_target_frame, snap_prev_target_frame, snap_peer_frame;
    uint32_t        snap_last_peer_ts;
    uint32_t        snap_frameskip, snap_epoch_frame, snap_watermark = 0;
    int             snap_crc_clean = 1;
    uint8_t         snap_epoch_value, snap_epoch_is_auto;
    netplay_mode_type snap_mode;
    netplay_state_t snap_state_tmp, snap_prev_state_sent;
    netplay_state_t snap_hist[ROLLBACK_PACKET_HISTORY];
    int             snap_hist_valid[ROLLBACK_PACKET_HISTORY];

    pthread_mutex_lock(&handle->sync_mutex);
    snap_target_frame      = handle->target_frame;
    snap_prev_target_frame = handle->prev_target_frame;
    snap_peer_frame        = handle->peer_frame;
    snap_last_peer_ts      = handle->last_peer_timestamp;
    snap_frameskip         = handle->frame_skip;
    snap_epoch_frame       = handle->frameskip_epoch_frame;
    snap_epoch_value       = handle->frameskip_epoch_value;
    snap_epoch_is_auto     = handle->frameskip_epoch_is_auto;
    snap_mode              = handle->mode;
    snap_state_tmp         = handle->state_tmp;
    snap_prev_state_sent   = handle->prev_state_sent;
    if (snap_mode == NETPLAY_MODE_ROLLBACK) {
        for (int i = 0; i < ROLLBACK_PACKET_HISTORY; i++) {
            if (snap_target_frame >= (uint32_t)i) {
                uint32_t h_frame = snap_target_frame - i;
                int h_idx = (int)(h_frame % ROLLBACK_RING_FRAMES);
                snap_hist[i] = handle->frame_history[h_idx].local_state;
                snap_hist_valid[i] = 1;
            } else {
                snap_hist_valid[i] = 0;
            }
        }
        /* Honest desync detector: advertise the CRC of our confirmed watermark
         * (a FINAL frame) instead of frame-1 (still-predicted).  Computed here
         * under sync_mutex since it scans frame_history + rollback fields.     */
        snap_watermark = netplay_confirmed_watermark(handle);
        /* Don't ADVERTISE a CRC for a frame whose local capture may still be
         * the stale pre-correction one — the peer would compare its final
         * state against our stale one and log a spurious DESYNC.            */
        snap_crc_clean = (!handle->crc_dirty || snap_watermark < handle->crc_dirty_low);
    }
    pthread_mutex_unlock(&handle->sync_mutex);

    /* Advertise target_frame VERBATIM, matching how the sender applies its
     * own local input from frame_history[target_frame] with no offset -- so
     * the peer stores/applies it at the same ring index.                    */
    msg.u.data.peer_frame      = htonl(snap_target_frame);
    msg.u.data.peer_frame_prev = htonl(snap_prev_target_frame);

    /* Build the local state snapshot. We must NOT strip or filter inputs
     * (like START+SELECT or in-menu) because altering the input stream
     * sent to the peer breaks lockstep determinism. */
    encode_peer_state(&msg.u.data.peer_state_tmp, &snap_state_tmp);

    /* N-1 History implementation (Legacy lockstep redundancy) */
    encode_peer_state(&msg.u.data.peer_state_prev, &snap_prev_state_sent);

    /* Extended 16-frame history for Rollback mode.
     * This guarantees that even if 15 UDP packets are lost, the 16th packet
     * will instantly recover all missing inputs without desyncing! */
    if (snap_mode == NETPLAY_MODE_ROLLBACK) {
        for (int i = 0; i < ROLLBACK_PACKET_HISTORY; i++) {
            if (snap_hist_valid[i]) {
                encode_peer_state(&msg.u.data.rollback_history[i], &snap_hist[i]);
            } else {
                memset(&msg.u.data.rollback_history[i], 0, sizeof(netplay_state_t));
            }
        }
    }

    msg.u.data.timestamp      = htonl(netplay_get_ticks_ms());
    msg.u.data.echo_timestamp = htonl(snap_last_peer_ts);

    msg.u.data.peer_peer_frame = htonl(snap_peer_frame);
    msg.u.data.peer_frame_skip = snap_frameskip;

    msg.u.data.frameskip_epoch_frame = htonl(snap_epoch_frame);
    msg.u.data.frameskip_epoch_value = snap_epoch_value;
    msg.u.data.frameskip_epoch_is_auto = snap_epoch_is_auto;

    /* Honest desync detector, send side: send the CRC of our CONFIRMED
     * WATERMARK (a FINAL state -- see netplay_confirmed_watermark).
     * Throttled via the shared NETPLAY_CRC_FRAME_WANTED gate -- producer and
     * consumer cadence must never drift apart.                               */
    if (snap_mode == NETPLAY_MODE_ROLLBACK && snap_watermark > 0 &&
        NETPLAY_CRC_FRAME_WANTED(snap_watermark) &&
        snap_crc_clean && handle->rollback_capture_checksum)
    {
        msg.u.data.state_checksum = htonl(handle->rollback_capture_checksum(snap_watermark));
        msg.u.data.checksum_frame = htonl(snap_watermark);
    } else {
        msg.u.data.state_checksum = 0;
        msg.u.data.checksum_frame = 0;
    }
    int ret = handle->send_pkt_data(handle, &msg);

    /* Update N-1 history has been moved to netplay_post_frame_net
     * so that retransmissions don't overwrite the previous history.       */

    return ret;
}

/* Send a JOIN request (client -> host), advertising mode/build/game.      */
int netplay_send_join(netplay_t *handle){
    netplay_msg_t msg;
    memset(&msg, 0, sizeof(msg));

    /* Atomic increment: packet_uid races with send_data on another thread. */
    uint32_t uid  = __sync_add_and_fetch(&handle->packet_uid, 1);
    msg.packetid  = htonl(uid);
    msg.msg_type  = htonl(NETPLAY_MSG_JOIN);
    msg.u.join.frame_skip = handle->frame_skip;
    msg.u.join.mode       = handle->mode;
    msg.u.join.is_auto_frameskip = (uint8_t)handle->is_auto_frameskip;
    msg.u.join.time       = htonl(handle->basetime);
    /* Build-compatibility handshake fields.                                */
    msg.u.join.protocol    = htonl((uint32_t)NETPLAY_PROTOCOL_VERSION);
    msg.u.join.state_limit = htonl((uint32_t)ROLLBACK_STATE_SIZE_LIMIT);
    msg.u.join.max_frames  = htonl((uint32_t)ROLLBACK_MAX_FRAMES);
    msg.u.join.ring_budget = htonl((uint32_t)ROLLBACK_RING_RAM_BUDGET);
    NLOG("netplay_send_join calling send_pkt_data");
    int ret = handle->send_pkt_data(handle, &msg);
    NLOG("netplay_send_join send_pkt_data returned %d", ret);
    return ret;
}

/* Send a JOIN_ACK reply (host -> client), authoritative for mode/samplerate/
 * game/build limits.                                                       */
int netplay_send_join_ack(netplay_t *handle){
    netplay_msg_t msg;
    memset(&msg, 0, sizeof(msg));

    uint32_t uid  = __sync_add_and_fetch(&handle->packet_uid, 1);
    msg.packetid  = htonl(uid);
    msg.msg_type  = htonl(NETPLAY_MSG_JOIN_ACK);
    msg.u.join.frame_skip = handle->frame_skip;
    msg.u.join.mode       = handle->mode;
    msg.u.join.is_auto_frameskip = (uint8_t)handle->is_auto_frameskip;
    msg.u.join.time       = htonl(handle->basetime);
    /* Advertise the host's effective MAME samplerate; the client adopts it
     * for the session so both savestates are layout-identical.             */
    msg.u.join.sound_rate = htonl((uint32_t)myosd_droid_get_effective_sound_rate());
    strncpy(msg.u.join.game_name, handle->game_name, MAX_GAME_NAME - 1);
    msg.u.join.game_name[MAX_GAME_NAME - 1] = '\0';
    /* Build-compatibility handshake fields.                                */
    msg.u.join.protocol    = htonl((uint32_t)NETPLAY_PROTOCOL_VERSION);
    msg.u.join.state_limit = htonl((uint32_t)ROLLBACK_STATE_SIZE_LIMIT);
    msg.u.join.max_frames  = htonl((uint32_t)ROLLBACK_MAX_FRAMES);
    msg.u.join.ring_budget = htonl((uint32_t)ROLLBACK_RING_RAM_BUDGET);
    
    NLOG("send join ack for %s with basetime:%s", handle->game_name, ctime(&handle->basetime));
    
    int ret = handle->send_pkt_data(handle, &msg);
    NLOG("send join ack send_pkt_data returned %d", ret);
    return ret;
}

/* Symmetric start barrier: tell the peer we are ready to run the first
 * gameplay frame.  Sent repeatedly (UDP is lossy) until both sides agree.   */
int netplay_send_ready(netplay_t *handle){
    netplay_msg_t msg;
    memset(&msg, 0, sizeof(msg));

    if (!handle->has_connection)
        return 0;

    uint32_t uid  = __sync_add_and_fetch(&handle->packet_uid, 1);
    msg.packetid  = htonl(uid);
    msg.msg_type  = htonl(NETPLAY_MSG_READY);
    return handle->send_pkt_data(handle, &msg);
}

/* Advertise our total registered savestate size so the peer can detect an
 * incompatible save layout at game start (see NETPLAY_MSG_STATE_SIZE).
 * Uses the rollback bridge to read the live size; 0 if unavailable (the
 * receiver treats 0 as "not yet known" and keeps waiting).                 */
int netplay_send_state_size(netplay_t *handle){
    netplay_msg_t msg;
    memset(&msg, 0, sizeof(msg));

    if (!handle->has_connection)
        return 0;

    uint32_t sz = handle->rollback_get_state_size ? (uint32_t)handle->rollback_get_state_size() : 0;
    uint32_t uid  = __sync_add_and_fetch(&handle->packet_uid, 1);
    msg.packetid  = htonl(uid);
    msg.msg_type  = htonl(NETPLAY_MSG_STATE_SIZE);
    msg.u.state_size.state_size = htonl(sz);
    return handle->send_pkt_data(handle, &msg);
}

/* Ask the peer to enter a mid-game state-resync episode.  Bare message;
 * idempotency lives in netplay_resync_begin on the receive side.           */
int netplay_send_resync(netplay_t *handle){
    netplay_msg_t msg;
    memset(&msg, 0, sizeof(msg));

    if (!handle->has_connection)
        return 0;

    uint32_t uid  = __sync_add_and_fetch(&handle->packet_uid, 1);
    msg.packetid  = htonl(uid);
    msg.msg_type  = htonl(NETPLAY_MSG_RESYNC);
    return handle->send_pkt_data(handle, &msg);
}

/* Send a DISCONNECT burst (5x, since UDP may drop it right before teardown). */
int netplay_send_disconnect(netplay_t *handle){
    netplay_msg_t msg;
    memset(&msg, 0, sizeof(msg));

    if (!handle->has_connection)
        return 0;
        
    uint32_t uid  = __sync_add_and_fetch(&handle->packet_uid, 1);
    msg.packetid  = htonl(uid);
    msg.msg_type  = htonl(NETPLAY_MSG_DISCONNECT);
    
    NLOG("sending disconnect message to peer (5 bursts to ensure delivery)");
    int ret = 0;
    /* Send multiple times: UDP is lossy and we are about to tear down the
     * socket, so we want the peer to receive at least one copy.           */
    for (int i = 0; i < 5; i++) {
        ret = handle->send_pkt_data(handle, &msg);
        usleep(10000); /* 10 ms */
    }
    return ret;
}

/* ============================================================
 * SECTION 5 -- Session lifecycle
 * Handle init/mode/reset, connection warnings and the mid-game RESYNC
 * trigger.  netplay_resync_begin() and netplay_warn_disconnect() are
 * forward-declared in section 3 for netplay_read_data() to call.
 * ============================================================ */

/* Reset the handle for a new session (called at connect and disconnect).
 * Preserves frame_skip/mode/game_name across the reset instead of zeroing
 * them like the rest of the struct, since those are set by the UI before
 * netplayInit runs.                                                       */
int netplay_init_handle(netplay_t *handle){
    netplay_mode_type preserved_mode = handle->mode;
    char temp_game_name[MAX_GAME_NAME] = "";
    if (handle->game_name[0] != '\0') {
        strncpy(temp_game_name, handle->game_name, MAX_GAME_NAME - 1);
    }
    
    /* Preserve frame_skip across reset: it may have been set by the UI
     * before netplayInit was called. Default to 2 if never set.           */
    int saved_frame_skip = (handle->frame_skip > 0) ? handle->frame_skip : 2;
    int saved_auto = handle->is_auto_frameskip;

    /* Wake any game thread that may be sleeping on the cond before we
     * destroy the primitives.  Harmless if not yet initialised (zero-init
     * pthread_mutex_t / pthread_cond_t are valid on Linux / Android Bionic
     * and equivalent to PTHREAD_MUTEX_INITIALIZER / PTHREAD_COND_INITIALIZER). */
    pthread_mutex_lock(&handle->sync_mutex);
    pthread_cond_broadcast(&handle->sync_cond);
    pthread_mutex_unlock(&handle->sync_mutex);

    pthread_mutex_destroy(&handle->sync_mutex);
    pthread_cond_destroy(&handle->sync_cond);

    /* A prior session may have dropped mid-transfer (boot sync or a RESYNC)
     * with one of these still allocated; the memset below would orphan them. */
    if (handle->sync_state_buffer)  { free(handle->sync_state_buffer);  handle->sync_state_buffer  = NULL; }
    if (handle->sync_chunk_bitmap)  { free(handle->sync_chunk_bitmap);  handle->sync_chunk_bitmap  = NULL; }
    if (handle->sync_pending_buffer){ free(handle->sync_pending_buffer);handle->sync_pending_buffer= NULL; }

    memset(handle, 0, sizeof(netplay_t));

    /* Re-initialise synchronisation primitives after the memset.          */
    pthread_mutex_init(&handle->sync_mutex, NULL);
    pthread_cond_init(&handle->sync_cond, NULL);
    
    handle->has_received_data = 0;
    for (int i = 0; i < EARLY_BUFFER_SIZE; i++) {
        handle->early_peer_frame[i] = 0xFFFFFFFF;
    }
    for (int i = 0; i < ROLLBACK_MAX_FRAMES; i++) {
        handle->frame_history[i].frame = 0xFFFFFFFF;
    }

    handle->frame_skip        = saved_frame_skip;
    handle->is_auto_frameskip = saved_auto;
    handle->mode              = preserved_mode;
    if (handle->mode == 0) handle->mode = NETPLAY_MODE_LOCKSTEP; // default
    
    /* Rollback + Auto means ADAPTIVE input delay -- starts at the minimum and
     * tracks half the smoothed RTT.  A manual (non-zero) UI value still
     * means a FIXED delay.                                                 */
    if (handle->mode == NETPLAY_MODE_ROLLBACK && handle->is_auto_frameskip) {
        handle->frame_skip = NETPLAY_INPUT_DELAY_MIN;
    }
    
    handle->peer_frame_skip   = handle->frame_skip;
    
    if (temp_game_name[0] != '\0') {
        strncpy(handle->game_name, temp_game_name, MAX_GAME_NAME - 1);
    }
    
    handle->has_connection  = 0;
    handle->rollback_enabled = 1; /* will be evaluated at game-start */

    /* Back to the full ring until the next game's size gate re-derives it. */
    myosd_netplay_ring_frames = ROLLBACK_MAX_FRAMES;

    time(&handle->basetime);

    return 1;
}

/* has_connection edge tracking for the game-start bootstrap (myosd-netplay.
 * cpp's netplay_iu_on_game_start); was_connected itself isn't consumed
 * elsewhere, it's just latched per-connection. */
void netplay_track_connection(netplay_t *handle)
{
    static bool was_connected = false;
    if (handle) {
        if (handle->has_connection) {
            was_connected = true;
        } else if (was_connected) {
            was_connected = false;
        }
    }
}

/* Boot-time initial state sync / mid-game RESYNC episode.  Returns true if
 * myosd_netplay_input_update() must return immediately this call (either
 * because the savestate layout was found incompatible, or because a resync
 * episode just completed and the current frame must be discarded).
 * Talks to the MAME side only through handle->rollback_* (agnostic function
 * pointers, wired by myosd-netplay.cpp's netplay_iu_on_game_start) and the
 * myosd_netplay_* control helpers (myosd-netplay.h). */
bool netplay_initial_sync(netplay_t *handle)
{
    if (!(handle && handle->has_connection && handle->has_begun_game &&
          handle->mode == NETPLAY_MODE_ROLLBACK &&
          handle->rollback_enabled && !handle->initial_sync_complete))
        return false;

    /* Is this episode a mid-game resync (vs the boot-time initial
     * sync)?  Latched once at entry.  On a resync, cancel every
     * in-flight local correction first -- the episode replaces the
     * timeline wholesale, so a pending FF/rollback would replay into
     * garbage. */
    bool is_resync = (handle->resync_active != 0);
    if (is_resync) {
        if (myosd_netplay_get_ff_active()) {
            myosd_netplay_set_ff_active(0);
            myosd_netplay_set_audio_mute(0);
            myosd_netplay_set_selfcheck_probe(0);
        }
        myosd_netplay_rollback_reset_for_resync();
    }

    /* No-transfer deterministic boot (GGPO model), the default path:
     * both peers booted the same game deterministically, so only a
     * LOCAL capture of slot 0 is needed and the sync is marked
     * complete immediately.  A mid-game RESYNC always transfers
     * (below), unaffected by this path. */
    if (!is_resync && !NETPLAY_ROLLBACK_INITIAL_STATE_TRANSFER) {
        if (handle->rollback_capture_state)
            handle->rollback_capture_state(0);
        handle->initial_sync_complete = 1;
        NLOG("ROLLBACK: initial state transfer DISABLED (GGPO deterministic boot); captured slot 0 locally");
    }

    /* Savestate-layout compatibility probe (legacy transfer path):
     * confirm both peers register the same savestate size before the
     * host captures/injects anything -- a mismatch means different
     * save_item layouts, so both peers fall back to LOCKSTEP. */
    if (!is_resync && NETPLAY_ROLLBACK_INITIAL_STATE_TRANSFER &&
        handle->rollback_get_state_size) {
        uint32_t local_size = (uint32_t)handle->rollback_get_state_size();
        auto probe_start = std::chrono::steady_clock::now();
        constexpr long SIZE_PROBE_TIMEOUT_MS = 15000;
        while (handle->has_connection && handle->peer_state_size == 0) {
            netplay_send_state_size(handle);
            struct timespec req = {0, 33000000}; // 33ms
            nanosleep(&req, NULL);
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - probe_start).count();
            if (el > SIZE_PROBE_TIMEOUT_MS) {
                NLOG("state-size probe timed out (peer_size still 0) - proceeding as compatible");
                break;
            }
        }
        /* Burst a few more so the peer still receives OUR size even if
         * it left its own probe loop a beat before us.               */
        for (int r = 0; r < 3 && handle->has_connection; r++)
            netplay_send_state_size(handle);

        if (handle->peer_state_size != 0 && local_size != 0 &&
            handle->peer_state_size != local_size) {
            handle->mode = NETPLAY_MODE_LOCKSTEP;
            handle->rollback_enabled = 0;
            handle->initial_sync_complete = 1;
            NLOG("INCOMPATIBLE save layout (local=%u peer=%u bytes) - falling back to LOCKSTEP",
                 local_size, handle->peer_state_size);
            if (handle->netplay_warn)
                handle->netplay_warn((char*)"TOAST:@not_rollback_compatible");
            return true;   /* next frame: mode=LOCKSTEP, block skipped, barrier runs */
        }
        NLOG("save layout OK (local=%u peer=%u bytes)", local_size, handle->peer_state_size);
    }

    /* The host captures + injects + streams its state ONLY when
     * transferring (legacy boot sync) or for a mid-game RESYNC.  In the
     * default no-transfer boot this is skipped entirely (handled above). */
    if ((NETPLAY_ROLLBACK_INITIAL_STATE_TRANSFER || is_resync) &&
        handle->player1 == 1 && !handle->sync_state_buffer && handle->has_joined && handle->rollback_capture_state) {
        /* Capture at the CURRENT frame: 0 for the boot-time initial
         * sync (identical to the old hardcoded slot 0), the live frame
         * for a mid-game resync -- the client ADOPTS this number.     */
        uint32_t sync_frame = handle->frame;
        handle->sync_state_frame = sync_frame;
        if (is_resync) {
            /* Drop every PRE-resync ring capture: frame numbering is
             * continuous, so stale slots still resolve by frame id
             * but hold DIVERGED old-timeline states the CRC detector
             * would flag right after a perfectly good resync.  Slots
             * refill lazily from the synced frame onward. */
            if (handle->rollback_cleanup_states)
                handle->rollback_cleanup_states();
            pthread_mutex_lock(&handle->sync_mutex);
            handle->confirmed_watermark = (sync_frame > 0) ? sync_frame - 1 : 0;
            pthread_mutex_unlock(&handle->sync_mutex);
        }
        handle->rollback_capture_state(sync_frame);
        const uint8_t *buffer = handle->rollback_get_state_buffer(sync_frame);
        handle->sync_state_size = handle->rollback_get_state_size();

        /* CRITICAL: Copy the RAW (pre-load) buffer BEFORE calling inject.
         * inject() stores the data in the slot and calls load(), which triggers
         * device postload callbacks that may modify machine state.
         * We send this raw_A buffer to the Client so BOTH machines will apply
         * load() exactly once (via inject), ending in the same f(raw_A) state. */
        handle->sync_state_buffer = (uint8_t *)malloc(handle->sync_state_size);
        if (handle->sync_state_buffer && buffer) {
            memcpy(handle->sync_state_buffer, buffer, handle->sync_state_size);
            NLOG("Host captured %u bytes for %s sync at frame %u", handle->sync_state_size,
                 is_resync ? "RESYNC" : "initial", sync_frame);
        }

        if (!is_resync) {
            /* BOOT-TIME: apply to HOST machine (ONE load) immediately.
             * Machine = f(raw_A), same as the Client after receiving.
             * Tolerable in-timeslice only because both machines are
             * still at t~0 and both apply it symmetrically.          */
            if (handle->rollback_inject_state && buffer && handle->sync_state_size > 0) {
                handle->rollback_inject_state(sync_frame, buffer, handle->sync_state_size);
            }
        } else {
            /* MID-GAME: an in-timeslice load here would corrupt the
             * rewound timers against the live basetime.  Latch a
             * clean-boundary deferred load instead; the client mirrors
             * with a store-only + deferred load of the same raw_A. */
            myosd_netplay_rollback_arm_pending_load(sync_frame);
        }

        handle->sync_total_chunks = (handle->sync_state_size + STATE_CHUNK_SIZE - 1) / STATE_CHUNK_SIZE;
        handle->sync_last_acked_chunk = 0;
    }
    static int log_throttle = 0;
    if (log_throttle++ % 60 == 0) {
        NLOG("ROLLBACK: Waiting for initial savestate sync...");
    }
    myosd_netplay_sync_poll();

    /* Returning early is not enough -- MAME still advances the frame
     * unless we loop here.  Hard deadline so heavy packet loss on the
     * STATE_CHUNK/ACK handshake reports a hangup instead of hanging
     * forever. */
    auto sync_wait_start = std::chrono::steady_clock::now();
    /* 60s, not 15s: the peer may legitimately still be LOADING the
     * machine (CHD games take tens of seconds).  A dead connection
     * still exits early via has_connection (socket watchdog), so the
     * higher ceiling only costs time when the peer is genuinely slow. */
    constexpr long SYNC_INITIAL_TIMEOUT_MS = 60000;
    int resync_iter = 0;
    while (!handle->initial_sync_complete && handle->has_connection) {
        myosd_netplay_sync_poll();

        /* Resync reliability: the request travels over lossy UDP and
         * the peer may not have latched the episode yet, so while
         * frozen retransmit it every ~0.5s and (host only) re-arm the
         * chunk blast every ~3s until the final ACK arrives.  Both
         * are idempotent no-ops once the peer is in. */
        if (is_resync) {
            resync_iter++;
            if ((resync_iter & 31) == 0)
                netplay_send_resync(handle);
            if (handle->player1 == 1 && (resync_iter % 192) == 0 &&
                !handle->initial_sync_complete) {
                pthread_mutex_lock(&handle->sync_mutex);
                handle->sync_last_acked_chunk = 0;   /* re-arm blast   */
                pthread_mutex_unlock(&handle->sync_mutex);
                NLOG("RESYNC: re-arming state blast (no final ACK yet)");
            }
        }

        /* CLIENT savestate injection on the GAME thread: the network
         * thread received + uncompressed the host's savestate into
         * sync_pending_buffer; inject it here so the rollback bridge
         * callbacks are guaranteed non-null.  A second load mirrors
         * the HOST's own double-load, so both converge to g(f(raw_A)). */
        if (handle->player1 != 1 && handle->sync_state_received &&
            !handle->initial_sync_complete && handle->rollback_inject_state) {
            if (is_resync) {
                /* Mid-game resync: ADOPT the host's frame and apply
                 * the state via the clean-boundary deferred load --
                 * store-only here, since an in-timeslice load
                 * mid-game corrupts the rewound timers vs the live
                 * basetime.  Mirrors the host. */
                uint32_t f = handle->sync_pending_frame;
                /* Free the DIVERGED pre-resync ring captures BEFORE
                 * storing the synced state (mirrors the host side). */
                if (handle->rollback_cleanup_states)
                    handle->rollback_cleanup_states();
                if (handle->rollback_store_state)
                    handle->rollback_store_state(f, handle->sync_pending_buffer, handle->sync_pending_size);
                pthread_mutex_lock(&handle->sync_mutex);
                handle->frame        = f;
                handle->target_frame = f;
                handle->confirmed_watermark = (f > 0) ? f - 1 : 0;
                pthread_mutex_unlock(&handle->sync_mutex);
                myosd_netplay_rollback_arm_pending_load(f);
                free(handle->sync_pending_buffer);
                handle->sync_pending_buffer = NULL;
                handle->sync_state_received = 0;
                handle->initial_sync_complete = 1;
                handle->resync_active = 0;
                {   /* completion stamp: same wall clock as netplay.cpp */
                    struct timeval tv;
                    gettimeofday(&tv, NULL);
                    handle->resync_last_done_ms =
                        (uint32_t)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
                }
                NLOG("RESYNC done (CLIENT): adopted host frame %u", f);
                break;
            }
            handle->rollback_inject_state(0, handle->sync_pending_buffer, handle->sync_pending_size);
            if (handle->rollback_load_state)
                handle->rollback_load_state(0);   /* symmetric second load */
            free(handle->sync_pending_buffer);
            handle->sync_pending_buffer = NULL;
            handle->sync_state_received = 0;
            handle->initial_sync_complete = 1;
            NLOG("CLIENT: injected initial savestate on game thread (symmetric double load)");
            break;
        }

        struct timespec req = {0, 16000000}; // 16ms
        nanosleep(&req, NULL);

        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - sync_wait_start).count();
        if (elapsed_ms > SYNC_INITIAL_TIMEOUT_MS) {
            NLOG("ROLLBACK: initial sync TIMEOUT after %lld ms (role=%s) - aborting netplay",
                 (long long)elapsed_ms, handle->player1 ? "HOST" : "CLIENT");
            if (handle->netplay_warn)
                handle->netplay_warn((char*)"TOAST:@sync_timeout");
            handle->has_connection = 0;
            break;
        }
    }
    {
        auto sync_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - sync_wait_start).count();
        NLOG("TELEM %s_done role=%s duration_ms=%lld complete=%d",
             is_resync ? "resync" : "initial_sync",
             handle->player1 ? "HOST" : "CLIENT", (long long)sync_ms,
             handle->initial_sync_complete);
    }
    NLOG("ROLLBACK: %s savestate sync finished!", is_resync ? "RESYNC" : "Initial");
    if (is_resync) {
        /* Return WITHOUT advancing netplay this call.  MAME finishes
         * the current (discarded) frame on the old timeline and the
         * deferred load applies the synced state at the next clean
         * scheduler boundary; the next myosd_netplay_input_update
         * resumes the normal rollback path from the adopted frame. */
        return true;
    }
    return false;
}

/* Symmetric start barrier: block until BOTH peers confirm they are
 * ready for the first gameplay frame, so the one that finishes
 * loading first doesn't race permanently ahead of the other
 * (bounded to ~1 RTT).  Runs once per game start; MAME stays frozen
 * here just like the rollback initial-sync loop above. */
void netplay_start_barrier(netplay_t *handle)
{
    if (!(handle && handle->has_connection && handle->has_begun_game && !handle->local_ready))
        return;

    bool sync_ready = (handle->mode != NETPLAY_MODE_ROLLBACK) ||
                      handle->initial_sync_complete || !handle->rollback_enabled;
    if (!sync_ready)
        return;

    handle->local_ready = 1;
    __sync_synchronize();
    netplay_send_ready(handle);

    auto barrier_start = std::chrono::steady_clock::now();
    /* 60s for the same reason as SYNC_INITIAL_TIMEOUT_MS: the peer
     * may still be loading a CHD game.  Exits early on peer_ready
     * or disconnection, so the ceiling is only paid on real skew. */
    constexpr long READY_BARRIER_TIMEOUT_MS = 60000;
    while (handle->has_connection && !handle->peer_ready) {
        netplay_send_ready(handle);   /* resend: UDP is lossy */
        struct timespec req = {0, 8000000}; // 8ms
        nanosleep(&req, NULL);
        auto el = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - barrier_start).count();
        if (el > READY_BARRIER_TIMEOUT_MS) {
            NLOG("START BARRIER: timeout after %lld ms (role=%s), proceeding",
                 (long long)el, handle->player1 ? "HOST" : "CLIENT");
            break;
        }
    }
    /* Small burst so the peer still gets our READY even if it exits
     * the barrier slightly before us (we stop resending on exit). */
    for (int r = 0; r < 3 && handle->has_connection; r++)
        netplay_send_ready(handle);

    auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - barrier_start).count();
    NLOG("TELEM start_barrier_done role=%s waited_ms=%lld peer_ready=%d",
         handle->player1 ? "HOST" : "CLIENT", (long long)waited, handle->peer_ready);
}

/* Set the session mode (LOCKSTEP/ROLLBACK).                               */
void netplay_set_mode(netplay_t *handle, int mode) {
    if (handle) handle->mode = (netplay_mode_type)mode;
}

/* C-linkage wrapper around netplay_set_mode, for JNI/dlsym callers.       */
extern "C" void myosd_netplay_set_mode(int mode){
    netplay_get_handle()->mode = (netplay_mode_type)mode;
}

/* Runtime backing for NETPLAY_CRC_DETECTOR_ENABLED (netplay.h): a plain
 * global rather than a netplay_t field, since it's a local perf/diagnostic
 * choice, not session/protocol state -- each peer decides independently and
 * nothing negotiates it over the wire.  Default on.                        */
static bool g_crc_detector_enabled = true;

bool myosd_netplay_crc_detector_runtime_enabled(void) {
    return g_crc_detector_enabled;
}

void netplay_set_crc_detector_enabled(bool enabled) {
    g_crc_detector_enabled = enabled;
}

/* Warn the user that the peer stopped responding; caller falls back to
 * offline play (this function only shows the message).                    */
void netplay_warn_hangup(netplay_t *handle)
{
    /* No TOAST* prefix -> Java shows it as a MODAL dialog (original behavior);
     * the "@key" is localized by resolveNpMsg in Emulator.netplayWarn.        */
    char msg[] = "@connection_lost";
    
    if(handle->netplay_warn!=0)
        handle->netplay_warn(msg);
    else
        printf("%s",msg);
}

/* Warn the user that the peer sent a DISCONNECT; caller falls back to
 * offline play (this function only shows the message).                    */
void netplay_warn_disconnect(netplay_t *handle)
{
    /* No TOAST* prefix -> Java shows it as a MODAL dialog (original behavior);
     * the "@key" is localized by resolveNpMsg in Emulator.netplayWarn.        */
    char msg[] = "@peer_disconnected";
    
    if(handle->netplay_warn!=0)
        handle->netplay_warn(msg);
    else
        printf("%s",msg);
}

/* Mid-game state RESYNC (rollback only).  Resets the sync-transfer machinery
 * so the initial-sync episode re-runs at the CURRENT frame; frame numbering
 * itself is not reset (in-flight DATA stays valid).  Idempotent and callable
 * from any thread.  Returns 1 if active, 0 if n/a.                         */
int netplay_resync_begin(netplay_t *handle, const char *origin)
{
    if (!handle || !handle->has_connection || !handle->has_begun_game)
        return 0;
    if (handle->mode != NETPLAY_MODE_ROLLBACK || !handle->rollback_enabled)
        return 0;

    pthread_mutex_lock(&handle->sync_mutex);
    if (handle->resync_active) {
        pthread_mutex_unlock(&handle->sync_mutex);
        return 1;                       /* episode already in flight        */
    }
    /* Never disturb a still-running BOOT initial sync, and absorb late
     * duplicate RESYNC packets right after an episode completed.           */
    if (!handle->initial_sync_complete ||
        (netplay_get_ticks_ms() - handle->resync_last_done_ms) < 3000) {
        pthread_mutex_unlock(&handle->sync_mutex);
        return 0;
    }

    handle->resync_active         = 1;
    handle->initial_sync_complete = 0;
    handle->sync_state_received   = 0;
    handle->sync_state_compressed = 0;
    handle->sync_total_chunks     = 0;
    handle->sync_last_acked_chunk = 0;
    handle->sync_chunks_received  = 0;
    handle->sync_state_frame      = 0;
    handle->sync_pending_frame    = 0;
    if (handle->sync_state_buffer)  { free(handle->sync_state_buffer);  handle->sync_state_buffer  = NULL; }
    if (handle->sync_chunk_bitmap)  { free(handle->sync_chunk_bitmap);  handle->sync_chunk_bitmap  = NULL; }
    if (handle->sync_pending_buffer){ free(handle->sync_pending_buffer);handle->sync_pending_buffer= NULL; }

    /* The pre-resync timeline is abandoned: no pending correction may fire
     * into it, and no stale history entry may ever confirm/mismatch again. */
    handle->requires_rollback = 0;
    handle->crc_dirty         = 0;
    handle->crc_dirty_low     = 0;
    /* Detector state must not straddle the resync -- an un-reset
     * confirmed_watermark stays frozen pre-resync and keeps comparing a
     * diverged old-timeline CRC forever.  Completion points (myosd-netplay.cpp)
     * raise it back to F-1.                                                 */
    handle->confirmed_watermark  = 0;
    handle->last_crc_match_frame = 0;
    handle->consecutive_desyncs  = 0;
    /* The N-1 pair is broadcast every packet; left stale it would advertise
     * an OLD-timeline input for a now-zeroed frame.  Zero it (and the input
     * snapshots) so prediction restarts from silence on both sides.        */
    handle->prev_target_frame = 0;
    memset(&handle->prev_state_sent, 0, sizeof(handle->prev_state_sent));
    memset(&handle->state_tmp,       0, sizeof(handle->state_tmp));
    memset(&handle->state,           0, sizeof(handle->state));
    memset(&handle->peer_state,      0, sizeof(handle->peer_state));
    memset(handle->frame_history, 0, sizeof(handle->frame_history));
    for (int i = 0; i < ROLLBACK_MAX_FRAMES; i++)
        handle->frame_history[i].frame = 0xFFFFFFFF;   /* same invalid mark as init */
    for (int i = 0; i < EARLY_BUFFER_SIZE; i++)
        handle->early_peer_frame[i] = 0xFFFFFFFF;
    pthread_mutex_unlock(&handle->sync_mutex);

    NLOG("RESYNC begin (%s) role=%s frame=%u", origin,
         handle->player1 ? "HOST" : "CLIENT", handle->frame);
    return 1;
}

/* JNI-callable entry point: latch a user-triggered resync and notify the
 * peer (see the matching declaration comment in netplay.h).               */
int myosd_netplay_request_resync(void)
{
    netplay_t *handle = netplay_get_handle();
    if (!handle) return 0;
    if (!netplay_resync_begin(handle, "local request"))
        return 0;
    /* Burst against UDP loss; the sync wait loop keeps retransmitting.     */
    for (int i = 0; i < 3; i++)
        netplay_send_resync(handle);
    if (handle->netplay_warn)
        handle->netplay_warn((char*)"TOAST:@resyncing");
    return 1;
}

/* ============================================================
 * SECTION 6 -- External predicates / UI hooks
 * Session-state queries for core-emu callers (DAV HACK sites, machine.cpp)
 * and the two Java-UI setters.  Debug-only symbols live in myosd-netplay.cpp
 * (per-item CRC diagnostics) and are not duplicated here.
 * ============================================================ */

/* Session-state predicates for original-MAME DAV HACK sites: is_active =
 * any mode; is_rollback / is_lockstep = active AND that mode.  Plain C++
 * linkage (all callers are .cpp TUs, no C/JNI boundary).                  */
bool myosd_netplay_is_active() {
    netplay_t *handle = netplay_get_handle();
    return (handle && handle->has_connection);
}

/* Session is up AND it is rollback (see the predicates block above).      */
bool myosd_netplay_is_rollback() {
    netplay_t *handle = netplay_get_handle();
    return (handle && handle->has_connection && handle->mode == NETPLAY_MODE_ROLLBACK);
}

/* Session is up AND it is lockstep (see the predicates block above).      */
bool myosd_netplay_is_lockstep() {
    netplay_t *handle = netplay_get_handle();
    return (handle && handle->has_connection && handle->mode == NETPLAY_MODE_LOCKSTEP);
}

/* Field accessors so ORIGINAL-MAME core files can read handle fields without
 * the netplay_t layout / netplay.h.                                        */
time_t myosd_netplay_basetime(void) {
    netplay_t *handle = netplay_get_handle();
    return (handle && handle->has_connection) ? handle->basetime : 0;
}

/* Session is up AND the game has begun (see the accessors block above).   */
bool myosd_netplay_has_begun(void) {
    netplay_t *handle = netplay_get_handle();
    return (handle && handle->has_connection && handle->has_begun_game);
}

/* UI: enable/disable the session; sends DISCONNECT when turning it off.   */
void netplay_ui_set_connection(netplay_t *handle, int value)
{
    if (handle) {
        if (value == 0 && handle->has_connection) {
            netplay_send_disconnect(handle);
        }
        handle->has_connection = value;
    }
}

/* UI: apply the input-delay/frameskip slider value (0 = Auto).  Mid-session
 * only the HOST may change it (client ignores, to avoid asymmetric state),
 * via the epoch mechanism so both peers switch at the same frame.         */
void netplay_ui_set_delay(netplay_t *handle, int value)
{
    if (value > 0) {
        value = value * 2; // Map UI visual frames to internal polling ticks (2 polls per frame)
    }
    static int frame_delay = -1;
    if (handle)
    {
        if (!handle->has_joined)
        {
            // Before connection: apply directly to frame_skip so it gets
            // sent to the Client in the JOIN_ACK packet.
            if (value > 0) {
                handle->frame_skip = value;
                handle->peer_frame_skip = value;
                handle->is_auto_frameskip = 0;
            } else {
                handle->is_auto_frameskip = 1;
            }
        }
        else if (handle->player1 && value != frame_delay)
        {
            // Mid-game: only server (player1) can trigger the handshake.
            // Client ignores changes to avoid asymmetric state.  Always
            // schedule an epoch broadcast -- even Auto<->Fixed at the same
            // numeric value -- so the peer's is_auto_frameskip flips too.
            // Under sync_mutex: the game thread's send path snapshots these
            // same fields, so an unlocked write here could be read half-
            // written and get stuck mismatched forever.
            pthread_mutex_lock(&handle->sync_mutex);
            uint8_t  new_auto  = (value == 0) ? 1 : 0;
            uint32_t new_value = (value == 0) ? handle->frame_skip : (uint32_t)value;

            if (new_auto != handle->is_auto_frameskip || handle->frame_skip != new_value) {
                uint32_t epoch_offset = (new_value > handle->frame_skip) ? 30 : 120;
                uint32_t nf = handle->target_frame + epoch_offset;
                /* Monotonic epoch id: receivers dedupe on a strictly
                 * increasing epoch_frame, so a new one must never go
                 * at-or-below one already broadcast, or the peer silently
                 * drops it and the delays stay diverged forever.           */
                if (nf <= handle->last_epoch_received)
                    nf = handle->last_epoch_received + 1;
                handle->frameskip_epoch_frame   = nf;
                handle->frameskip_epoch_value   = (uint8_t)new_value;
                handle->frameskip_epoch_is_auto = new_auto;
                /* The peer echoes our pending epoch back in its own DATA
                 * packets; pre-marking it received makes us ignore that echo
                 * (it is OUR epoch) and anchors the monotonic guard above to
                 * the newest epoch ever broadcast.                          */
                handle->last_epoch_received     = nf;
            }
            handle->is_auto_frameskip = new_auto;
            pthread_mutex_unlock(&handle->sync_mutex);
        }
    }
    frame_delay = value;
}

