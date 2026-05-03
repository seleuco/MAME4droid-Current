// license:BSD-3-Clause
// copyright-holders:Filipe Paulino (FlykeSpice)
/***************************************************************************

    myosd_renderer.h

    Abstract class for all renderers supported by MAME4droid

***************************************************************************/

#pragma once

#ifndef MYOSD_RENDERER
#define MYOSD_RENDERER

#include "emucore.h"
#include "render.h"

#include "myosd.h"

#include <string>
#include <vector>

class myosd_renderer
{
public:
	virtual void init_renderer() {}
	virtual void end_renderer() {}
    virtual void sync_state(const render_primitive_list* primlist) = 0;
	virtual void render() = 0;
	virtual void on_emulatedsize_change(int width, int height) = 0;

	virtual void set_shader(const char* shader) = 0;

	virtual ~myosd_renderer() = default;
};

#endif //MYOSD_RENDERER
