// license:BSD-3-Clause
// copyright-holders: David Valdeita (Seleuco)
/***************************************************************************

    myosd_slim_state.cpp

    See myosd_slim_state.h.  Reproduces ram_state's stream layout (a 32-byte
    header followed by each entry's data, blocks compacted with stride removed,
    in registration order) but, per the myosd_save_hacks policy, DROPS immutable
    bulk entries and ZEROS dead-scratch ones.  The netplay rollback bridge's
    offset/CRC walks stay valid because they apply the SAME classification.
    presave/postload are dispatched exactly as ram_state does, so devices that
    pack/unpack state behave identically.

    The per-entry class (normal/exclude/canon) is decided ONCE per game and
    cached, so the per-frame capture and the CRC walks pay a single array lookup
    instead of re-running the name-pattern hacks on every entry every frame.

***************************************************************************/

#include "emu.h"
#include "save.h"

#include "myosd_slim_state.h"
#include "myosd_save_hacks.h" /* per-game exclusion policy */

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

// matches HEADER_SIZE in save.cpp do_write (the bridge's walks assume it too)
constexpr size_t SLIM_HEADER_SIZE = 32;

// Cached per-entry class, built once per game (keyed on registration_count).
// The build runs under s_class_mutex; the per-frame fast path is a lock-free
// atomic-acquire load of the count plus a plain vector index (the vector is
// fully written before the count is published, and only ever rebuilt at game
// start / after invalidate, never concurrently with steady-state play).
std::atomic<int>     s_class_count{ -1 };
std::vector<uint8_t> s_class;
std::mutex           s_class_mutex;

} // anonymous namespace


uint8_t myosd_slim_state::entry_class(save_manager &save, int index)
{
	const int count = save.registration_count();
	if (s_class_count.load(std::memory_order_acquire) != count)
	{
		std::lock_guard<std::mutex> lk(s_class_mutex);
		if (s_class_count.load(std::memory_order_relaxed) != count)
		{
			// driver source file (e.g. ".../williams/midtunit.cpp"), used to
			// scope name-generic exclusion rules to the right driver
			const char *driver_src = save.machine().system().type.source();
			s_class.assign(count, (uint8_t)CLASS_NORMAL);
			for (int i = 0; i < count; i++)
			{
				void *base; uint32_t vs, vc, bc, st;
				const char *name = save.indexed_item(i, base, vs, vc, bc, st);
				if (!name)
					continue;
				if (myosd_save_hack_exclude_from_rollback(driver_src, name))
					s_class[i] = (uint8_t)CLASS_EXCLUDE;
				else if (myosd_save_hack_canonicalize(name))
					s_class[i] = (uint8_t)CLASS_CANON;
			}
			s_class_count.store(count, std::memory_order_release);
		}
	}
	if (index < 0 || index >= (int)s_class.size())
		return (uint8_t)CLASS_NORMAL;
	return s_class[index];
}


void myosd_slim_state::invalidate_class_cache()
{
	std::lock_guard<std::mutex> lk(s_class_mutex);
	// Reset only the count, do NOT clear() the vector: a CRC walk on the network
	// thread may read it lock-free, and clearing/reallocating under it is a data
	// race.  The next build (game thread, at startup, before the net thread runs)
	// reassigns it; a stale lock-free read meanwhile returns a valid old byte.
	s_class_count.store(-1, std::memory_order_release);
}


size_t myosd_slim_state::get_size(save_manager &save)
{
	size_t total = SLIM_HEADER_SIZE;
	const int count = save.registration_count();
	for (int i = 0; i < count; i++)
	{
		void *base; uint32_t valsize, valcount, blockcount, stride;
		const char *name = save.indexed_item(i, base, valsize, valcount, blockcount, stride);
		if (!name)
			break;
		if (entry_class(save, i) == CLASS_EXCLUDE)
			continue;
		total += (size_t)valsize * valcount * blockcount;
	}
	// plus the ioport live-input tail (see save())
	total += save.machine().ioport().netplay_state_io(nullptr, nullptr);
	return total;
}


myosd_slim_state::myosd_slim_state(save_manager &save)
	: m_save(save)
{
	m_data.reserve(get_size(save));
}


bool myosd_slim_state::save()
{
	m_data.clear();
	m_data.resize(SLIM_HEADER_SIZE, 0); // deterministic placeholder; content unused

	// mirror ram_state: pack device state before reading it
	m_save.dispatch_presave();

	const int count = m_save.registration_count();
	for (int i = 0; i < count; i++)
	{
		void *base; uint32_t valsize, valcount, blockcount, stride;
		const char *name = m_save.indexed_item(i, base, valsize, valcount, blockcount, stride);
		if (!name)
			break;

		const uint8_t cls = entry_class(m_save, i);
		if (cls == CLASS_EXCLUDE)
			continue;

		const size_t blocksize = (size_t)valsize * valcount;
		if (cls == CLASS_CANON)
		{
			// dead scratch: store zeros so both peers agree (see the hack note)
			m_data.insert(m_data.end(), (size_t)blocksize * blockcount, (uint8_t)0);
			continue;
		}

		const uint8_t *data = reinterpret_cast<const uint8_t *>(base);
		for (uint32_t b = 0; b < blockcount; b++, data += stride)
			m_data.insert(m_data.end(), data, data + blocksize);
	}

	// ioport's live input state (frame latch, analog accumulators...) is not a
	// save_item: append it after the entries, so every slot, handover and resync
	// carries it while the netplay CRC/offset walks (which stop at the last entry)
	// never see it.
	ioport_manager &ioport = m_save.machine().ioport();
	const size_t tail = m_data.size();
	m_data.resize(tail + ioport.netplay_state_io(nullptr, nullptr));
	ioport.netplay_state_io(m_data.data() + tail, nullptr);
	return true;
}


bool myosd_slim_state::load()
{
	if (m_data.size() < SLIM_HEADER_SIZE)
		return false;

	size_t offset = SLIM_HEADER_SIZE;
	const int count = m_save.registration_count();
	for (int i = 0; i < count; i++)
	{
		void *base; uint32_t valsize, valcount, blockcount, stride;
		const char *name = m_save.indexed_item(i, base, valsize, valcount, blockcount, stride);
		if (!name)
			break;
		if (entry_class(m_save, i) == CLASS_EXCLUDE)
			continue;
		// CANON entries stay in the buffer as zeros and are written back like
		// any normal entry (the next instruction overwrites the scratch field).

		const size_t blocksize = (size_t)valsize * valcount;
		uint8_t *data = reinterpret_cast<uint8_t *>(base);
		for (uint32_t b = 0; b < blockcount; b++, data += stride)
		{
			if (offset + blocksize > m_data.size())
				return false;
			std::memcpy(data, m_data.data() + offset, blocksize);
			offset += blocksize;
		}
	}

	// the ioport live-input tail appended by save()
	ioport_manager &ioport = m_save.machine().ioport();
	if (offset + ioport.netplay_state_io(nullptr, nullptr) <= m_data.size())
		ioport.netplay_state_io(nullptr, m_data.data() + offset);

	// mirror ram_state: unpack device state after writing it
	m_save.dispatch_postload();
	return true;
}


void myosd_slim_state::set_data(const uint8_t *buffer, size_t size)
{
	m_data.assign(buffer, buffer + size);
}
