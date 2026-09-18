// license:BSD-3-Clause
// copyright-holders: David Valdeita (Seleuco)
/***************************************************************************

    myosd_save_hacks.cpp

    Per-game savestate hacks for netplay rollback (see myosd_save_hacks.h).
    To support a new game, add its immutable bulk save-entry name(s) below.

***************************************************************************/

#include "myosd_save_hacks.h"

#include <cstring>

namespace {

inline bool name_has(const char *name, const char *needle)
{
	return std::strstr(name, needle) != nullptr;
}

inline bool name_ends(const char *name, const char *suffix)
{
	size_t n = std::strlen(name), s = std::strlen(suffix);
	return n >= s && std::strcmp(name + n - s, suffix) == 0;
}

} // anonymous namespace


bool myosd_save_hack_exclude_from_rollback(const char *driver_src, const char *name)
{
	if (!name)
		return false;
	if (!driver_src)
		driver_src = "";

	// CPS-3: the 2MB data array of each SIMM flash -- game code/gfx/sound from CD,
	// read-only in play (~80MB total).  Keep the small controller regs, drop the
	// bulk array only.
	if (name_has(name, "simm") && name_has(name, "m_data"))
		return true;

	// CPS-3: decrypted program/gfx image (16MB), built once at init, mapped ROM.
	if (name_has(name, "decrypted_gamerom"))
		return true;

	// Midway DCS on T/Wolf-unit (mk2, mk3): banked sound ROM copied into a buffer,
	// read-only in play.  SCOPED here -- DCS2 variants keep sound data in RAM the
	// CPU uploads (mutable, and not in the cross-peer CRC: dropping it = silent desync).
	if ((name_has(driver_src, "midtunit") || name_has(driver_src, "midwunit")) &&
	    name_has(name, "m_sounddata"))
		return true;

	// Midway T/Wolf-unit: the GFX ROM is exposed as a big read-only ":video" share
	// (up to 12MB), auto-saved.  SCOPED by source -- a bare "video" name is writable
	// video RAM in other drivers (hp95lx, mgames, ...).  The mutable framebuffer is
	// a separate entry (m_local_videoram), kept.
	if ((name_has(driver_src, "midtunit") || name_has(driver_src, "midwunit")) &&
	    name_has(name, ":maincpu/0/:video"))
		return true;

	return false;
}


bool myosd_save_hack_desync_tolerant(const char *driver_src)
{
	if (!driver_src)
		return false;

	// CPS-3 (sfiii*, jojo*, redearth, warzard): after a state transfer/drop-in
	// the SH-2 (DRC) resumes a few cycles off the host, so the snapshot catches
	// it mid-instruction and the one RAM block it is writing diverges though the
	// game is bit-fine.  Narrow but persistent -> can't tell from a real desync
	// generically, so here we wait for a MASSIVE divergence before warning.
	if (name_has(driver_src, "cps3"))
		return true;

	return false;
}


bool myosd_save_hack_crc_include(const char *name)
{
	if (!name)
		return false;

	// Konami video chips (tilemap/sprite generators, PSAC, LVC, priority encoder):
	// VRAM, sprite RAM and sprite buffer live in the device, not a "memory/" share.
	// Matched by role: the bare "Konami 05" prefix also caught the 054539 PCM
	// chip's sound RAM, which is not CPU-written video state.
	static const char *const video_roles[] = {
		"Tilemap Generator", "Sprite Generator", "PSAC", "LVC", "Priority Encoder"
	};
	if (!name_has(name, "Konami 05") ||
	    !(name_ends(name, "/m_ram") || name_ends(name, "/m_buffer") || name_ends(name, "/m_videoram")))
		return false;
	for (const char *role : video_roles)
		if (name_has(name, role))
			return true;

	return false;
}


bool myosd_save_hack_canonicalize(const char *name)
{
	if (!name)
		return false;

	// CPS-3 SH-2: m_sh2_state->ea is effective-address scratch, dead across
	// boundaries.  A drop-in joiner reaches the handover with a different leftover
	// than the host -> bogus ":maincpu" CRC desync.  Zero it so both agree.
	if (name_has(name, "m_sh2_state->ea"))
		return true;

	return false;
}
