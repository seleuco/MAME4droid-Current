// license:BSD-3-Clause
// copyright-holders:Filipe Paulino (FlykeSpice)
/***************************************************************************

    myosd_renderer.h

    Abstract class for all renderers supported by MAME4droid

***************************************************************************/

#pragma once

#ifndef MYOSD_RENDERER
#define MYOSD_RENDERER

#include "emu/emucore.h"
#include "emu/render.h"

#include <string>
#include <vector>

class myosd_renderer
{
public:
	virtual void render(const render_primitive_list& primlist) = 0;
	virtual void on_emulatedsize_change(int width, int height) = 0;

	//FlykeSpice: Can't be virtual because they are static members...
	//virtual std::vector<std::string> get_shaders_supported() = 0;

	virtual void set_shader(const char* shader) = 0;

	virtual ~myosd_renderer() = default;
};

#endif //MYOSD_RENDERER
