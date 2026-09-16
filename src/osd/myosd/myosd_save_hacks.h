// license:BSD-3-Clause
// copyright-holders: David Valdeita (Seleuco)
/***************************************************************************

    myosd_save_hacks.h

    Per-game savestate hacks for netplay rollback -- one place to add or
    remove game-specific tweaks to what the rollback snapshot contains.

    Today it holds the rollback EXCLUSION list: save entries that hold large
    IMMUTABLE data (game ROM flashed into SIMM/flash devices, decrypted
    program/gfx images, banked sound ROM) which never change during gameplay.
    Dropping them from the per-frame rollback snapshot shrinks huge states
    (CPS-3 ~150MB, MK3/DCS) below the size budget so rollback can engage, while
    the data stays live in RAM (so a load that skips it is correct).

    Keep this list here, isolated from the generic capture mechanism
    (myosd_slim_state) and from the netplay protocol, so games can be added or
    dropped by editing one table.

***************************************************************************/

#ifndef MYOSD_SAVE_HACKS_H
#define MYOSD_SAVE_HACKS_H

#pragma once

// True if this save entry is immutable bulk to drop from the rollback snapshot.
// `driver_src` (system().type.source()) scopes rules whose name is too generic
// to be safe everywhere.  Substring match, so a miss just re-includes it (bigger
// state, never a wrong drop).  Used by both the slim capture and the CRC walks.
bool myosd_save_hack_exclude_from_rollback(const char *driver_src, const char *name);

// True if this save entry is dead scratch (set and used within one instruction,
// never live across a boundary) whose leftover differs between peers -- e.g. a
// drop-in joiner -- and false-trips the detector.  The capture stores ZEROS for
// it on both peers so the CRC agrees; the reload is harmless (next write sets it).
bool myosd_save_hack_canonicalize(const char *name);

// True if this driver's harmless post-handover CPU-phase noise makes the plain
// desync detector cry wolf, so netplay waits for a BROAD (multi-section) RAM
// divergence before warning for it.  `driver_src` = system().type.source().
bool myosd_save_hack_desync_tolerant(const char *driver_src);

// True if this non-"memory/" entry is CPU-written video RAM kept inside a chip
// device, to add to the cross-peer desync CRC: without it a divergence confined
// to tilemap/sprite RAM is invisible.  Arrays only, never timing state.
bool myosd_save_hack_crc_include(const char *name);

#endif // MYOSD_SAVE_HACKS_H
