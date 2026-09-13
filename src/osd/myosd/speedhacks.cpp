// license:BSD-3-Clause
// copyright-holders: Filipe Paulino (FlykeSpice)
/***************************************************************************

    speedhacks.cpp

    Speedhacks for MAME4Droid

***************************************************************************/


#include "speedhacks.h"

#include "mame/irem/nl_kidniki.h"
#include "devices/machine/netlist.h"

#include <vector>

std::array<myosd_speedhack, 2> my_speedhacks =
{{
	//Taito Air System
	{
		.id = 0,
		.title = "Taito Air System",
		.desc  = "This is only works for Taito Air games Top Landing & Air Inferno",

		.func = [](machine_config &mconfig)
		{
			std::string game_name = mconfig.gamedrv().name;
			if (!game_name.starts_with("topland") && !game_name.starts_with("ainferno"))
				return;

			//very hackish but it's the only way we get to remove perfect quantum from machine configuration
			auto token = mconfig.begin_configuration(mconfig.root_device());
			mconfig.set_perfect_quantum("");
		}
	},

	//Irem M62
	//this makes netlist device run at half frequency
	{
		.id = 1,
		.title = "Irem M62",
		.desc = "This only works for games that run on Irem M62 such as: Spelunker, Kid Niki, Kung Fu Master and Lode Runner",

		.func = [](machine_config &mconfig)
		{
			auto nl_device = mconfig.device<netlist_mame_sound_device>("irem_audio:snd_nl");
			if (nl_device)
			{
				nl_device->set_setup_func([](netlist::nlparse_t &setup)
				{
					LOCAL_SOURCE(kidniki)
					INCLUDE(kidniki)
					PARAM(Solver.FREQ, 24000) //Halve the Solver's running frequency
					//__android_log_print(ANDROID_LOG_DEBUG, "hacks", "NETLIST SETUP!!");
				});
			}
		}
	}
}};

//JNI functions

struct speedhack_option
{
	int id;
	const char* title;
	const char* desc;
};

extern "C"
speedhack_option* myosd_get_speedhacks(int* n)
{
	static std::vector<speedhack_option> list;

	if (list.empty())
	{
		for (const auto& speedhack : my_speedhacks)
		{
			speedhack_option entry;
			entry.id    = speedhack.id;
			entry.title = speedhack.title;
			entry.desc  = speedhack.desc;

			list.push_back(entry);
		}
	}

	*n = list.size();

	return list.data();
}

extern "C"
void myosd_toggle_speedhack(int id, bool flag)
{
	for (auto& speedhack : my_speedhacks)
	{
		if (speedhack.id == id)
		{
			speedhack.enabled = flag;
			break;
		}
	}
}
