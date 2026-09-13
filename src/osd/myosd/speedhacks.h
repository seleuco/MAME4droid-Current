// license:BSD-3-Clause
// copyright-holders: Filipe Paulino (FlykeSpice)
/***************************************************************************

    speedhacks.h

    Speedhacks for MAME4Droid

***************************************************************************/

#include <array>
#include <functional>

#include "emu/emu.h"

struct myosd_speedhack
{
	int id;
	const char* title;
	const char* desc;
	bool enabled = false;

	std::function<void(machine_config &mconfig)> func;
};

extern std::array<myosd_speedhack, 2> my_speedhacks;
