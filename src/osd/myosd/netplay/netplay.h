// license:BSD-3-Clause
// copyright-holders: David Valdeita (Seleuco) 
/***************************************************************************

    netplay.h

    MAME4droid (Lock-Step + Rollback) Netplay Architecture

    Struct/type definitions below are ordered bottom-up by C++ value-
    embedding (netplay_state_t is the innermost leaf, embedded by value in
    netplay_frame_history_t and the netplay_msg_* wire structs; netplay_t,
    the main session handle, embeds all of them and so must come last) --
    this order can't be changed without switching to pointers/forward
    declarations.  The function declarations near the end of the file are
    free of that constraint and follow netplay.cpp's own most-central-first
    layout instead (see the FILE LAYOUT note there).

***************************************************************************/

#ifndef netplay_h
#define netplay_h

#include <stdint.h>
#include <time.h>
#include <pthread.h>  /* required for pthread_mutex_t / pthread_cond_t */

#include "myosd.h"
    
    typedef enum {
        NETPLAY_TYPE_SKT = 1,
    } netplay_impl_type;

    typedef enum {
        NETPLAY_MODE_LOCKSTEP = 1,
        NETPLAY_MODE_ROLLBACK,
    } netplay_mode_type;

    /* Rollback ring: MAX_FRAMES = ceiling depth (~2s) and the static slot count;
     * effective depth is adaptive (ROLLBACK_RING_FRAMES).  STATE_SIZE_LIMIT: bigger
     * states fall back to lockstep -- 24MB lets ~20.5MB games (Caveman Ninja) in.
     * Both peers exchange the limit in the JOIN handshake and must agree. */
    #define ROLLBACK_MAX_FRAMES         120
    #define ROLLBACK_STATE_SIZE_LIMIT   (24 * 1024 * 1024)
    /* Loss redundancy: each DATA packet re-carries the last PACKET_HISTORY input
     * frames, so up to N-1 packets lost in a row recover with no rollback.  It is
     * the bulk of the packet, so 8 (~133ms at 60fps) halves it for metered relays;
     * never a correctness knob.  Wire size -- peers must match (v12). */
    #define ROLLBACK_PACKET_HISTORY 8

    /* Adaptive depth = min(MAX_FRAMES, RAM_BUDGET/state_size), lockstep below
     * MIN_FRAMES; both peers derive it the same (budget travels in the handshake).
     * Floor 48 keeps the full frame-advantage margin (needs ring >= ADVANTAGE(24)
     * + MARGIN(16) + skip) and still lets ~42MB states (BUDGET/48) roll back. */
    #define ROLLBACK_RING_RAM_BUDGET  (2048u * 1024u * 1024u)
    #define ROLLBACK_MIN_FRAMES       48

    /* Initial-state transfer.  1 (default): host streams its state, client adopts
     * it (same clean-boundary handover as a drop-in) so both share the host's
     * scheduler basetime.  0 (GGPO): both boot the pinned state and only trade
     * inputs.  Why 1: with 0 the peers reach frame 0 at different emulated times,
     * so a phase-sensitive read (RTC over serial, a counter seed) diverges and
     * snowballs -- seen on Tekken.  Must match on both peers (v12). */
    #define NETPLAY_ROLLBACK_INITIAL_STATE_TRANSFER 1

    /* Effective ring depth for THIS game (set by the myosd_netplay.cpp size
     * gate, ROLLBACK_MAX_FRAMES otherwise).  Defined in netplay.cpp.       */
    extern uint32_t myosd_netplay_ring_frames;
    #define ROLLBACK_RING_FRAMES (myosd_netplay_ring_frames)

    /* NLOG master switch: 0 for release .so builds (compiles all NLOG out;
     * netplay_warn messages and the stats overlay are unaffected).  Set to 1
     * only for field diagnosis; return to 0 before release. */
    #define NETPLAY_LOG_ENABLED 0

    /* Protocol/build handshake: bump on any wire or determinism-critical change;
     * peers exchange it (+ size/ring limits) in JOIN and refuse a mismatched build
     * rather than desync.  v13 (current): INPUT_NEED/INPUT_RESEND heal input holes
     * past PACKET_HISTORY, and the rollback state carries ioport's live input state
     * (new state size).  v12: clean-boundary capture, PACKET_HISTORY 8, transfer
     * on.  Never mix builds.  (Older changelog in git.) */
    #define NETPLAY_PROTOCOL_VERSION 13

    /* Frame-advantage cap: max frames the local machine may run ahead before
     * stalling for the peer (else the screens drift).  Sized to absorb mobile RTT
     * jitter without stalling constantly, well short of the rollback window. */
    #define ROLLBACK_MAX_FRAME_ADVANTAGE 24
    
    /* Dynamic rate control: corrects sustained frame-advantage drift by
     * nudging MAME's speed factor (inaudible within MAX_DELTA) toward zero
     * advantage every EVAL_FRAMES, clamped by DEADBAND so a healthy session
     * idles at 1000.                                                       */
    #define NETPLAY_RATE_DEADBAND_FRAMES 2
    #define NETPLAY_RATE_MAX_DELTA       15   /* per-mille: speed in [985,1015] */
    #define NETPLAY_RATE_EVAL_FRAMES     4  /* >= frames between speed steps */

    /* CRC desync detector master switch.  false removes all CRC overhead
     * (diagnosis-only, never triggers corrective action); true hashes every
     * save_item on frames matching NETPLAY_CRC_FRAME_WANTED (both peers
     * apply the same predicate) and flags a mismatch in the log.  Runtime
     * flag (Java pref, default on): a peer with it off just always sends
     * checksum=0, which the receive side treats as "not present".         */
    bool myosd_netplay_crc_detector_runtime_enabled(void);
    #define NETPLAY_CRC_DETECTOR_ENABLED (myosd_netplay_crc_detector_runtime_enabled())
    #define NETPLAY_CRC_EVERY            5
    #define NETPLAY_CRC_FRAME_WANTED(f)  (NETPLAY_CRC_DETECTOR_ENABLED && \
                                          ((f) <= 10 || ((f) % NETPLAY_CRC_EVERY) == 0))

    /* Desync detector: whole-RAM CRC, toast on any confirmed mismatch.  CPS-3
     * (myosd_save_hack_desync_tolerant) also needs >= MIN of the 16 sections to
     * differ -- it measures spread, not size, so it filters little on its own. */
    #define NETPLAY_CRC_SECTIONS        16
    #define NETPLAY_DESYNC_MIN_SECTIONS  3

    /* TOLERANT DRIVERS ONLY (CPS-3): distinct compared frames a mismatch must
     * survive before the TOAST (log/ITEM_DIFF still fire on the first one).
     * Not global -- waiting would un-anchor the probe from the root frame. */
    #define NETPLAY_DESYNC_MIN_CONFIRMS  3

    /* Frame-advantage stall cap, split SOFT/HARD: SOFT lets a comfortable
     * stall yield early for smoothness; HARD (within MARGIN of the ring's
     * safe_depth) is unbreakable, so the advantage can never exceed the
     * rollback window.  DISCONNECT_ITERS gives up if the peer is gone.     */
    #define ROLLBACK_STALL_HARD_MARGIN       16
    #define ROLLBACK_STALL_SOFT_ITERS        80
    #define ROLLBACK_STALL_DISCONNECT_ITERS  1500

    /* Grace period for transient send failures (e.g., Wi-Fi roaming/handoff).
     * Treats temporary sendto() errors (like ENETUNREACH) as packet loss 
     * rather than instantly killing the session. The timeout is sized just under
     * the peer's ~4.5s stall-disconnect budget to ensure sync disconnects. */
    #define NETPLAY_SEND_FAIL_GRACE_MS 4000

    /* Adaptive input delay, auto mode only: frame_skip tracks half the
     * smoothed RTT, clamped to MIN/MAX.
     * MAX 6 -> 4: buying the whole one-way trip with delay is what lockstep
     * does, and rollback is here to predict it instead.  Field-tested at
     * 210-230ms RTT, where 4 felt better than the 6 the cap was giving.      */
    #define NETPLAY_INPUT_DELAY_MIN 2
    #define NETPLAY_INPUT_DELAY_MAX 4

#ifndef MAX_GAME_NAME
#define MAX_GAME_NAME 64
#endif
    
#pragma pack(push, 1)
    typedef struct netplay_state{
        uint32_t digital;      /* MYOSD_* button/dpad bitmask                */
        float analog_x;        /* left stick / joystick X                    */
        float analog_y;        /* left stick / joystick Y                    */
        float analog_rx;       /* right stick X                              */
        float analog_ry;       /* right stick Y                              */
        float analog_lz;       /* left trigger (analog)                      */
        float analog_rz;       /* right trigger (analog)                     */
        uint16_t ext;          /* unused: never applied to emulation         */
        float mouse_x;         /* accumulated absolute mouse X               */
        float mouse_y;         /* accumulated absolute mouse Y               */
        uint32_t mouse_status; /* mouse button bitmask                       */
        float lightgun_x;      /* lightgun X                                 */
        float lightgun_y;      /* lightgun Y                                 */
    }netplay_state_t;

    /* Must be >= ROLLBACK_MAX_FRAMES * 2: during a rollback FF, handle->frame
     * rewinds while the PEER keeps advancing and sending confirmations, so
     * the gap can approach ROLLBACK_MAX_FRAMES.  Too small silently drops an
     * early confirmation (wrapped-slot overwrite) with no mismatch logged --
     * invisible desync.                                                    */
    #define EARLY_BUFFER_SIZE     (ROLLBACK_MAX_FRAMES * 2)

    /* Per-frame input snapshot stored in the rollback ring buffer.         */
    typedef struct netplay_frame_history {
        uint32_t        frame;              /* frame number this slot holds */
        netplay_state_t local_state;        /* our own input for this frame */
        netplay_state_t peer_state;         /* peer input: real or predicted */
        netplay_state_t applied_peer_state; /* what MAME actually executed */
        uint8_t         peer_confirmed; /* 1=real peer input received, 0=prediction */
    } netplay_frame_history_t;
    
    typedef enum {
        /* Per-frame input packet (both modes); carries the current frame's
         * input plus N-1/16-frame history for loss recovery.                */
        NETPLAY_MSG_DATA = 1,
        /* Client -> host: request to join, advertising mode/build/game.     */
        NETPLAY_MSG_JOIN,
        /* Host -> client: accept the join, advertising the session's mode,
         * samplerate, game and build/protocol limits (host is authoritative).*/
        NETPLAY_MSG_JOIN_ACK,
        /* Either side leaving the session; burst-sent since UDP is lossy.   */
        NETPLAY_MSG_DISCONNECT,
        /* One fragment of a streamed savestate transfer (initial sync or a
         * mid-game RESYNC).                                                 */
        NETPLAY_MSG_STATE_CHUNK,
        /* Client -> host: acknowledges the next expected STATE_CHUNK id.    */
        NETPLAY_MSG_STATE_ACK,
        /* Symmetric start barrier: both peers confirm ready before either
         * advances the first gameplay frame.                                */
        NETPLAY_MSG_READY,
        /* Debug-only: HOST ships its per-save_item CRC table for a desynced
         * frame; CLIENT diffs and logs only the differing items by name.    */
        NETPLAY_MSG_ITEMCRC_CHUNK,
        /* User-triggered mid-game state resync (rollback only); both sides
         * re-run the initial-sync episode at the current frame.             */
        NETPLAY_MSG_RESYNC,
        /* Savestate-size compatibility probe at game start; a size mismatch
         * (different save_items registered) falls back to LOCKSTEP for both
         * peers before any state is captured/injected.                     */
        NETPLAY_MSG_STATE_SIZE,
        /* Header-only NAT hole-punch probe (internet play): opens/keeps the
         * sender's NAT mapping toward the peer; receiver ignores it.       */
        NETPLAY_MSG_PUNCH,
        /* Rollback input hole: the sender's confirmed watermark is stuck on a
         * frame older than any DATA still re-carries; asks for a resend.   */
        NETPLAY_MSG_INPUT_NEED,
        /* Answer to INPUT_NEED: the sender's local inputs from base_frame.  */
        NETPLAY_MSG_INPUT_RESEND,
        /* Quick chat: a predefined phrase id (never free text), rendered by
         * each peer in its own language.                                    */
        NETPLAY_MSG_CHAT
    } netplay_msg_type;
    
    typedef struct netplay_msg_join {
        uint8_t frame_skip;    /* sender's input delay / jitter buffer       */
        uint8_t mode;          /* netplay_mode_type: LOCKSTEP or ROLLBACK    */
        /* Host-authoritative Auto/Fixed delay choice, sent in JOIN_ACK, so a
         * Client left on its own local Auto pref doesn't keep adjusting
         * frame_skip out from under a Host that picked a fixed delay.      */
        uint8_t is_auto_frameskip;
        uint32_t time;         /* sender's basetime (RTC epoch to pin)       */
        /* HOST's effective -samplerate, adopted by the client -- the
         * sound-stream buffers live in the savestate and depend on it, so a
         * mismatch breaks state transfer, CRC comparison and audio.  Sent in
         * JOIN_ACK (host is authoritative).                                 */
        uint32_t sound_rate;
        char game_name[MAX_GAME_NAME]; /* sender's selected/running game    */
        /* Build-compatibility handshake (NETPLAY_PROTOCOL_VERSION).
         * Appended LAST so an older build's JOIN/JOIN_ACK (zero-filled union
         * via memset) reads here as protocol==0 -> refused cleanly.          */
        uint32_t protocol;      /* NETPLAY_PROTOCOL_VERSION of the sender    */
        uint32_t state_limit;   /* sender's ROLLBACK_STATE_SIZE_LIMIT        */
        uint32_t max_frames;    /* sender's ROLLBACK_MAX_FRAMES              */
        uint32_t ring_budget;   /* sender's ROLLBACK_RING_RAM_BUDGET         */
    }netplay_msg_join_t;

#define STATE_CHUNK_SIZE 1024

    typedef struct netplay_msg_state_chunk {
        uint32_t chunk_id;       /* index of this fragment, 0-based         */
        uint32_t total_chunks;   /* fragment count for the whole transfer   */
        uint32_t total_size;     /* total savestate byte size               */
        uint16_t chunk_size;     /* valid bytes in data[] (last chunk < full)*/
        uint8_t  data[STATE_CHUNK_SIZE]; /* fragment payload                */
        /* Frame the streamed state was captured at.  0 for boot-time sync;
         * host's live frame for a mid-game RESYNC (client adopts it as its
         * own frame counter for continuous numbering).                      */
        uint32_t sync_frame;
    } netplay_msg_state_chunk_t;

    /* Sliding-window ACK for the STATE_CHUNK transfer.                      */
    typedef struct netplay_msg_state_ack {
        uint32_t next_expected_chunk; /* lowest chunk_id not yet received   */
    } netplay_msg_state_ack_t;

    /* Savestate-layout compatibility probe (see NETPLAY_MSG_STATE_SIZE).    */
    typedef struct netplay_msg_state_size {
        uint32_t state_size;   /* sender's total registered savestate byte size */
    } netplay_msg_state_size_t;

    /* Input-hole resend request (see NETPLAY_MSG_INPUT_NEED).               */
    typedef struct netplay_msg_input_need {
        uint32_t frame;        /* first peer-input frame the sender is missing */
    } netplay_msg_input_need_t;

    #define NETPLAY_RESEND_MAX 16   /* inputs per INPUT_RESEND (fits the union) */

    /* Resent local inputs (see NETPLAY_MSG_INPUT_RESEND).                   */
    typedef struct netplay_msg_input_resend {
        uint32_t base_frame;   /* frame of states[0]                          */
        uint8_t  count;        /* valid entries in states[]                   */
        netplay_state_t states[NETPLAY_RESEND_MAX];
    } netplay_msg_input_resend_t;

    #define NETPLAY_CHAT_MAX_PHRASE 64   /* phrase ids accepted on the wire */

    /* Quick chat phrase (see NETPLAY_MSG_CHAT).                             */
    typedef struct netplay_msg_chat {
        uint16_t phrase;       /* predefined phrase id                        */
        uint32_t seq;          /* per-session counter: drops the burst copies */
    } netplay_msg_chat_t;

#define ITEMCRC_PER_CHUNK (STATE_CHUNK_SIZE / 4)   /* 256 uint32 CRCs per packet */

    /* Debug-only one-shot per-save_item CRC table fragment (see
     * NETPLAY_MSG_ITEMCRC_CHUNK).  Carries up to ITEMCRC_PER_CHUNK item CRCs of
     * the sender's slot for `frame`.  The receiver reassembles all chunks then
     * diffs against its own slot for `frame`. */
    typedef struct netplay_msg_itemcrc_chunk {
        uint32_t frame;         /* desync frame this CRC table is for       */
        uint32_t chunk_id;      /* index of this fragment, 0-based          */
        uint32_t total_chunks;  /* fragment count for the whole table       */
        uint32_t total_items;   /* total CRC entries across all chunks      */
        uint16_t chunk_items;   /* valid entries in crcs[] (last chunk < full) */
        uint32_t crcs[ITEMCRC_PER_CHUNK]; /* per-save_item CRC32 fragment   */
    } netplay_msg_itemcrc_chunk_t;
    
    typedef struct netplay_msg_data {
        uint8_t is_peer_paused; /* sender's local pause state              */
        uint32_t peer_frame;    /* sender's target_frame for peer_state_tmp */
        netplay_state_t peer_state_tmp; /* sender's input for peer_frame    */
        /* N-1 input piggybacking: every packet also carries the PREVIOUS frame's
         * inputs.  If peer_frame-1 packet was lost, the receiver recovers it
         * from (peer_frame_prev / peer_state_prev) without stalling.          */
        uint32_t peer_frame_prev;
        netplay_state_t peer_state_prev;
        uint32_t peer_peer_frame;  /* sender's view of OUR last frame (echo) */
        uint8_t peer_frame_skip;   /* sender's ACTUAL input delay this packet */
        uint32_t timestamp;        /* sender's clock at send time (RTT calc) */
        uint32_t echo_timestamp;   /* timestamp we last received, echoed back */
        uint32_t frameskip_epoch_frame; /* lockstep: frame the pending frameskip change applies at */
        uint8_t frameskip_epoch_value;  /* lockstep: the new frameskip value  */
        uint8_t frameskip_epoch_is_auto; /* is_auto_frameskip to adopt when the epoch above fires */

        /* Desync Detector: CRC32 of the savestate for a specific frame */
        uint32_t state_checksum;   /* CRC32 of our state at checksum_frame  */
        uint32_t checksum_frame;   /* frame the CRC above was computed for  */
        /* Per-section RAM fingerprint (1 byte each); only CPS-3 fills it, for
         * the broad-divergence test -- zero for every other driver. */
        uint8_t  state_section_fp[NETPLAY_CRC_SECTIONS];

        /* Extended history for Rollback mode: recovers input across up to
         * PACKET_HISTORY-1 consecutive dropped packets (loss-recovery only,
         * never correctness -- see the ROLLBACK_PACKET_HISTORY define). */
        netplay_state_t rollback_history[ROLLBACK_PACKET_HISTORY];
    }netplay_msg_data_t;
    
    typedef struct netplay_msg{
        uint32_t packetid;  /* monotonic per-sender id (see packet_uid)    */
        uint32_t msg_type;  /* netplay_msg_type, selects the union member  */
        union {
            netplay_msg_join_t join;
            netplay_msg_data_t data;
            netplay_msg_state_chunk_t state_chunk;
            netplay_msg_state_ack_t state_ack;
            netplay_msg_state_size_t state_size;
            netplay_msg_itemcrc_chunk_t itemcrc_chunk;
            netplay_msg_input_need_t input_need;
            netplay_msg_input_resend_t input_resend;
            netplay_msg_chat_t chat;
        }u;
    }netplay_msg_t;
#pragma pack(pop)
    
    typedef struct netplay
    {
        netplay_impl_type type;  /* transport backend (SKT)                */
        netplay_mode_type mode;  /* LOCKSTEP or ROLLBACK for this session   */
        unsigned player1;        /* 1 = we are the host, 0 = client         */

        volatile int has_connection;  /* a session is up (see myosd_netplay_is_active) */
        int has_joined;               /* JOIN/JOIN_ACK handshake completed  */
        /* Host only: start and last of the current run of JOINs, to spot a
         * JOIN_ACK that never lands.  A gap starts a new run, so a peer that
         * cancels and tries again is never judged on the old one. */
        uint32_t join_first_ms;
        uint32_t join_last_ms;
        /* Drop-in (host, rollback): the game runs on its own with the room
         * published, and nothing of netplay touches it until someone joins.
         * Set from the UI before netplayInit; 0 is every existing session. */
        int drop_in;
        /* Whether this game can hand over to a drop-in joiner: 0 not measured
         * yet, 1 the savestate fits the rollback ring, 2 too big ever to. */
        volatile int drop_in_state;
        /* The size behind that verdict, in KB, so a refusal can name it. */
        volatile int drop_in_state_kb;
        volatile int has_begun_game;  /* gameplay frames may advance        */
        volatile int is_peer_paused;  /* peer reported itself paused        */
        int is_auto_frameskip;        /* frame_skip is adaptive, not fixed  */
        int new_frameskip_set;        /* unused: reserved                   */

        char game_name[MAX_GAME_NAME]; /* local/adopted game name          */

        unsigned timeout_cnt;        /* consecutive lockstep-wait timeouts */
        uint32_t packet_uid;        /* always modified via __sync_add_and_fetch */
        uint32_t recv_packet_uid;   /* network-thread-only                       */
        uint32_t last_recv_time_ms; /* network-thread-only                       */
        /* First failed sendto of the CURRENT consecutive-failure run (0 = no
         * run in progress).  Transient send errors during a Wi-Fi roam / mesh
         * AP handoff are tolerated for NETPLAY_SEND_FAIL_GRACE_MS before the
         * session is declared dead (see skt_send_pkt_data).                  */
        uint32_t send_fail_since_ms;

        float local_abs_mouse_x; /* accumulated local mouse X (anchor-delta) */
        float local_abs_mouse_y; /* accumulated local mouse Y (anchor-delta) */

        netplay_state_t state;      /* input actually injected into MAME this frame */
        netplay_state_t peer_state; /* peer input actually injected this frame */

        netplay_state_t state_tmp;          /* local input staged for the outgoing packet */
        netplay_state_t peer_state_tmp;     /* peer input staged from the incoming packet */
        netplay_state_t peer_next_state_tmp; /* lockstep: pre-fetched next-frame peer input */

        uint32_t frame;               /* next frame to execute locally     */
        volatile uint32_t target_frame; /* frame our staged input is scheduled for */
        volatile uint32_t peer_frame;   /* peer's target_frame (their schedule) */
        volatile uint32_t peer_next_frame; /* lockstep: pre-fetched next target */
        volatile uint32_t peer_peer_frame; /* peer's echo of our last frame */
        volatile uint32_t last_received_peer_frame; /* highest peer_frame seen (advantage calc) */

        uint32_t frame_skip;          /* our input delay / jitter buffer   */
        volatile uint32_t peer_frame_skip; /* peer's advertised input delay */

        /* fs_adjust_last_frame: last frame the auto input-delay was
         * re-evaluated (game thread).  last_ack_send_ms: last ACK reply sent
         * (network thread), for the 8ms ACK rate limit.                     */
        uint32_t fs_adjust_last_frame;
        uint32_t last_ack_send_ms;

        uint32_t frameskip_epoch_frame; /* lockstep: frame our pending frameskip change applies at */
        uint8_t frameskip_epoch_value;  /* lockstep: the new frameskip value  */
        uint8_t frameskip_epoch_is_auto; /* is_auto_frameskip to adopt when the epoch above fires */
        uint32_t last_epoch_received;   /* last epoch_frame we already applied (dedupe) */

        uint32_t last_peer_timestamp; /* peer's timestamp, echoed back for their RTT */
        uint32_t smoothed_rtt;   /* slow EMA (a=1/8) of RTT, drives input delay */
        uint32_t fast_rtt;       /* fast EMA (a=1/4) of RTT                 */
        uint32_t rtt_mdev;       /* EMA of |rtt - smoothed_rtt| = jitter (RFC6298 mdev); overlay readout, both paths */
        uint32_t max_rtt_interval; /* decaying peak RTT (jitter envelope)   */
        uint32_t min_rtt_window;   /* decaying minimum RTT (jitter envelope) */
        /* Read-only floor/ceiling for the lobby report, kept in BOTH paths.
         * Deliberately not the envelope above: that one feeds the auto delay
         * and the frame-advantage stall, and is left at 0 by the rollback
         * path on purpose, so filling it would move both.                  */
        uint32_t tel_rtt_min;
        uint32_t tel_rtt_max;

        /* What rollback costs: how often a prediction missed, and the frames
         * those misses re-simulated. Ping says whether a link is fast, these
         * say whether the device can afford it. Game thread only. */
        uint32_t tel_rollbacks;
        uint32_t tel_rollback_frames;
        uint32_t rtt_update_time;  /* last lockstep auto-frameskip evaluation, ms */

        /* N-1 history: the most recently transmitted frame number and its
         * (already-filtered) inputs.  Piggybacked in every DATA packet so
         * the peer can recover a lost frame without stalling.               */
        uint32_t        prev_target_frame;
        netplay_state_t prev_state_sent;

        /* Loss counters for auto-frameskip                                  */
        uint32_t        recovery_n1_count; /* N-1 recoveries since last eval */

        time_t basetime; /* pinned RTC epoch, shared with the peer at JOIN */

        void *impl_data; /* transport-backend private data (see skt_netplay) */

        /* Rollback fields: frame_history is the per-frame input ring;
         * requires_rollback/rollback_to_frame flag+target a correction; the
         * rollback_* callbacks are myosd_netplay.cpp bridges so netplay.cpp
         * never depends on MAME core types directly.                       */
        netplay_frame_history_t frame_history[ROLLBACK_MAX_FRAMES];
        volatile int            requires_rollback;
        volatile uint32_t       rollback_to_frame;
        /* Generation counter bumped on every requires_rollback arm, so the
         * rollback step can detect an arm landing mid-FF and avoid dropping
         * it.                                                               */
        volatile uint32_t       rollback_arm_gen;
        /* CRC-detector dirty window -- a confirmed-with-mismatch frame enters
         * the watermark before its ring slot is re-captured, so CRCs for
         * frames >= crc_dirty_low are skipped while dirty (else spurious
         * "DESYNC DETECTED").                                               */
        volatile int            crc_dirty;     /* 1 while any frame's captured state may be stale */
        volatile uint32_t       crc_dirty_low; /* lowest frame affected by the dirty window */
        int                     rollback_enabled; /* 0 if this game's state is too large for rollback */

        volatile int            has_received_data; /* first peer packet arrived */
        uint32_t                early_peer_frame[EARLY_BUFFER_SIZE]; /* frame numbers of early-arrived peer input */
        netplay_state_t         early_peer_state[EARLY_BUFFER_SIZE]; /* peer input that arrived before we reached its frame */

        /* Reset with the rest of the detector on JOIN/JOIN_ACK/resync.
         * consecutive_desyncs = distinct frames mismatched in a row (any
         * agreement zeroes it); gates the toast, see MIN_CONFIRMS above.   */
        uint32_t                last_crc_match_frame;
        int                     consecutive_desyncs;

        /* Confirmed watermark: highest frame W where every frame up to W
         * holds a real peer-confirmed input, so CRC(W) is FINAL and safe to
         * compare.  Monotonic, advanced by netplay_confirmed_watermark().    */
        uint32_t                confirmed_watermark;

        /* Symmetric start barrier: local_ready / peer_ready must both be set
         * before the first gameplay frame advances on either side.           */
        volatile int            local_ready;
        volatile int            peer_ready;


        void (*rollback_capture_state)(uint32_t frame);   /* save machine state into the ring */
        void (*rollback_load_state)(uint32_t frame);       /* restore machine state from the ring */
        void (*rollback_cleanup_states)(void);             /* free every captured ring slot */

        uint32_t (*rollback_query_checksum)(uint32_t frame);   /* read a slot's cached CRC */
        uint32_t (*rollback_capture_checksum)(uint32_t frame); /* CRC of a slot, computing it if needed */
        void (*rollback_inject_state)(uint32_t frame, const uint8_t *buffer, uint32_t size); /* store + load a peer-sent buffer */
        /* Store-only variant of inject (no load, no postload): the mid-game
         * RESYNC stores the host's raw state into the ring slot and lets the
         * clean-boundary deferred load apply it, since an in-timeslice load
         * mid-game corrupts the rewound timers (only the boot-time initial
         * sync can load immediately, while both machines are still at t~0). */
        void (*rollback_store_state)(uint32_t frame, const uint8_t *buffer, uint32_t size); /* store a peer-sent buffer without loading it */
        const uint8_t* (*rollback_get_state_buffer)(uint32_t frame); /* raw bytes of a captured slot */
        size_t (*rollback_get_state_size)(void); /* byte size of one savestate */

        /* Initial state sync (boot-time savestate transfer, when enabled).  */
        volatile int initial_sync_complete; /* boot sync finished (or skipped) */
        uint8_t     *sync_state_buffer;     /* HOST: raw state buffer being streamed */
        uint32_t     sync_state_size;       /* HOST: byte size of sync_state_buffer */

        /* Client-side deferred injection (frame-1 desync fix): network thread
         * fills sync_pending_buffer + sets this flag; GAME thread injects it
         * once the rollback bridge callbacks are wired (injecting directly
         * from the network thread raced has_begun_game and could no-op).    */
        volatile int sync_state_received;   /* client's assembled buffer is ready to inject */
        uint8_t     *sync_pending_buffer;   /* CLIENT: buffer being assembled from chunks */
        uint32_t     sync_pending_size;     /* CLIENT: total_size of the transfer in progress */
        uint32_t     sync_total_chunks;     /* CLIENT: total_chunks of the transfer in progress */
        volatile uint32_t sync_next_expected_chunk; /* CLIENT: kept for compat, not used as gate */
        volatile uint32_t sync_last_acked_chunk;    /* HOST: last chunk confirmed by client */
        int sync_state_compressed;          /* buffer above is zlib-compressed */
        /* Out-of-order reception: bitmap tracks which chunks have been received.
         * sync_chunks_received counts the total; when == sync_total_chunks, done. */
        uint8_t     *sync_chunk_bitmap;     /* CLIENT: 1 bit per chunk, ceil(total_chunks/8) bytes */
        volatile uint32_t sync_chunks_received; /* CLIENT: count of bits set in sync_chunk_bitmap */

        /* Mid-game RESYNC (rollback only): resync_active flags an in-flight
         * episode; resync_last_done_ms de-dupes late UDP retries; sync_state_
         * frame/sync_pending_frame carry the host's captured frame and the
         * client's adopted frame counter.                                   */
        volatile int      resync_active;
        uint32_t          resync_last_done_ms;
        /* Quick chat: last seq sent / accepted (drops a burst's copies) and
         * the send time behind the 1-per-second limit.                      */
        uint32_t          chat_seq_sent;
        uint32_t          chat_seq_recv;
        uint32_t          chat_last_send_ms;
        uint32_t          sync_state_frame;
        volatile uint32_t sync_pending_frame;

        /* Peer's advertised total savestate size (0 = not yet received).
         * Compared against ours at game start; a mismatch means incompatible
         * save layouts -> lockstep fallback.                                 */
        volatile uint32_t peer_state_size;

        /* sync_mutex serialises the cross-thread fields above (peer_frame,
         * RTT, epoch...); sync_cond wakes the game thread's lockstep wait
         * instead of a busy-poll loop.                                      */
        pthread_mutex_t sync_mutex;
        pthread_cond_t  sync_cond;

        int (*read_pkt_data)(struct netplay *,netplay_msg_t *);  /* transport receive: 0 fatal, 1 valid, 2 foreign dropped */
        int (*send_pkt_data)(struct netplay *,netplay_msg_t *);  /* transport-backend send */
        void (*netplay_warn)(char *);                            /* UI toast/log callback */
        
    } netplay_t;
    
    
    /* Function declarations below follow netplay.cpp's own layout, most
     * central first: core handle accessor, the per-frame trunk, the
     * receive path, the send path, session lifecycle, then external
     * predicates/UI hooks and the two debug/deferred-load externs. */

    netplay_t * netplay_get_handle();               /* process-wide singleton handle */

    void netplay_pre_frame_net(netplay_t *handle);       /* called just before MAME executes a frame */
    void netplay_post_frame_net(netplay_t *handle);      /* called just after MAME executes a frame */

    int  netplay_read_data(netplay_t *handle);       /* read+process one incoming packet */

    int  netplay_send_data(netplay_t *handle);       /* send this frame's input packet */
    int  netplay_send_join(netplay_t *handle);       /* send JOIN (client -> host)   */
    int  netplay_send_join_ack(netplay_t *handle);   /* send JOIN_ACK (host -> client) */
    int  netplay_send_ready(netplay_t *handle);      /* send start-barrier READY     */
    int  netplay_send_resync(netplay_t *handle);     /* ask the peer for a mid-game resync */
    int  netplay_send_state_size(netplay_t *handle); /* advertise our savestate size */
    int  netplay_send_disconnect(netplay_t *handle); /* send a DISCONNECT burst      */

    int  netplay_init_handle(netplay_t *handle);         /* reset the handle for a new session */
    /* has_connection edge tracking / boot-time+RESYNC sync wait / symmetric
     * first-frame barrier -- the game-start bootstrap sequence called from
     * myosd_netplay_input_update() (myosd_netplay.cpp) each frame.  Pure
     * protocol/session logic.                     */
    void netplay_track_connection(netplay_t *handle);
    bool netplay_initial_sync(netplay_t *handle);        /* true = caller must return immediately */
    void netplay_start_barrier(netplay_t *handle);
    void netplay_set_mode(netplay_t *handle, int mode);  /* set LOCKSTEP/ROLLBACK    */
    void netplay_set_crc_detector_enabled(bool enabled); /* runtime toggle, see NETPLAY_CRC_DETECTOR_ENABLED above */
    void netplay_warn_hangup(netplay_t *handle);     /* show/log peer-timeout warning */
    /* User-triggered mid-game state resync (rollback only).  Latches the
     * episode locally and notifies the peer; returns 1 if latched, 0 if not
     * applicable (no session / lockstep / sync already running).  Safe to
     * call from any thread (Java UI via the netplayResync JNI bridge).       */
    int  myosd_netplay_request_resync(void);

    /* Quick chat: send predefined phrase `phrase` to the peer (Java netplay
     * dialog via the netplaySendChat JNI bridge).  1 if sent, 0 if no live
     * peer or rate-limited (1 per second).  Any thread.                     */
    int  myosd_netplay_send_chat(int phrase);

    /* Netplay session-state predicates (netplay.cpp).  C++ linkage (only .cpp
     * callers).  is_active = any mode; is_rollback / is_lockstep = active AND
     * that specific mode.                                                   */
    bool myosd_netplay_is_active(void);
    bool myosd_netplay_is_rollback(void);
    bool myosd_netplay_is_lockstep(void);

    /* Field accessors (defined in netplay.cpp) so core-emu callers can read the
     * handle fields they need without the netplay_t layout / this header:
     *   myosd_netplay_basetime()  -> handle->basetime while active, else 0
     *   myosd_netplay_has_begun() -> active AND the game has begun            */
    time_t myosd_netplay_basetime(void);
    bool   myosd_netplay_has_begun(void);

    void netplay_ui_set_connection(netplay_t *handle, int value); /* UI: enable/disable the session */
    void netplay_ui_set_delay(netplay_t *handle, int value);      /* UI: input-delay/frameskip slider */

    /* Debug (desync diagnosis): arm the in-memory APPLIED input ring-buffer
     * flush.  Defined in myosd_netplay.cpp; called from the desync triggers
     * here so the window around the first mismatch is dumped once, on the
     * game thread.                                                          */
    extern "C" void netplay_applied_ring_arm(const char *reason, uint32_t frame);
    extern "C" void netplay_applied_ring_reset(void);

    /* Perform a DEFERRED savestate reload at a clean scheduler boundary (never
     * inside the vblank timer callback -- load()->postload() would corrupt the
     * timer list mid-iteration).  Called from running_machine::run() between
     * timeslices.  No-op when nothing is pending.  Defined in myosd_netplay.cpp. */
    extern "C" void myosd_netplay_service_deferred_load(void);

    /* Arm a one-shot ring capture at the NEXT clean scheduler boundary.  Called
     * from the primary screen's vblank-OFF callback (screen.cpp) so the capture
     * lands at the frame tail (IRQ queue drained, field bit settled) instead of
     * mid-vblank.  Defined in myosd_netplay.cpp. */
    extern "C" void myosd_netplay_arm_boundary_capture(void);

    /* Drop-in handover: arm a clean-boundary capture of the handover frame (the
     * ring capture skips it) and poll _ready until it lands, so the slot is a
     * vblank-tail state that loads without a re-arm.  Defined in myosd_netplay.cpp. */
    extern "C" void myosd_netplay_arm_boot_slot_capture(void);
    extern "C" int  myosd_netplay_boot_slot_ready(void);

    /* myosd_netplay_set_speed (per-mille, 1000 = 100%) is declared in
     * myosd_netplay.h -- used by the dynamic rate controller in
     * myosd_netplay_input_update() (see NETPLAY_RATE_* above).             */

#endif
