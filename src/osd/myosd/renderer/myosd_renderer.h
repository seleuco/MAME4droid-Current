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

class myosd_renderer
{
public:
	virtual void render(const render_primitive_list& primlist) = 0;
	virtual void on_viewport_change(int width, int height) = 0;

	virtual ~myosd_renderer() = default;
};

#endif //MYOSD_RENDERER
