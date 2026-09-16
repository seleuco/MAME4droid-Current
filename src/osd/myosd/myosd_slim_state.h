// license:BSD-3-Clause
// copyright-holders: David Valdeita (Seleuco)
/***************************************************************************

    myosd_slim_state.h

    Slimmed savestate for netplay rollback.

    A drop-in replacement for MAME's ram_state that OMITS large immutable
    regions -- game ROM flashed into SIMM/flash devices, decrypted program/gfx,
    banked sound ROM -- which never change during gameplay.  Those regions stay
    live in RAM, so a load that skips them is correct: their current contents
    already equal the snapshot's.  This shrinks huge states (e.g. CPS-3 ~150MB,
    MK3/DCS) below the rollback size budget without touching the driver.

    Built on the public save_manager API (registration_count/indexed_item/
    dispatch_presave/dispatch_postload) plus one ioport DAV HACK accessor for the
    live input state appended after the entries.  Only the netplay rollback ring
    uses it; on-disk savestates keep MAME's full ram_state.

***************************************************************************/

#ifndef MYOSD_SLIM_STATE_H
#define MYOSD_SLIM_STATE_H

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class save_manager;

class myosd_slim_state
{
public:
	explicit myosd_slim_state(save_manager &save);

	// header + every non-excluded entry, matching the buffer save() produces
	static size_t get_size(save_manager &save);

	bool save();   // capture the live machine into m_data
	bool load();   // restore the live machine from m_data

	// same surface as ram_state's DAV HACK accessors, so the netplay bridge's
	// ring / compression / CRC paths consume it unchanged
	const std::vector<uint8_t> &get_data() const { return m_data; }
	void set_data(const uint8_t *buffer, size_t size);

	// Per-entry class, decided once per game and cached (the per-frame capture
	// and the netplay CRC walks use this instead of re-matching name patterns
	// every frame): NORMAL = copy the live bytes, EXCLUDE = drop from the buffer
	// entirely, CANON = keep the slot but store zeros (dead scratch).
	enum { CLASS_NORMAL = 0, CLASS_EXCLUDE = 1, CLASS_CANON = 2 };
	static uint8_t entry_class(save_manager &save, int index);

	// Drop the cached classification (call on game exit); the next capture
	// rebuilds it for the new machine.
	static void invalidate_class_cache();

private:
	save_manager        &m_save;
	std::vector<uint8_t> m_data;
};

// WHICH entries are dropped is the per-game policy in myosd_save_hacks.h
// (myosd_save_hack_exclude_from_rollback), shared by the capture and the
// netplay CRC/desync walks so their offsets stay in lockstep.

#endif // MYOSD_SLIM_STATE_H
