// license:BSD-3-Clause
// copyright-holders:Filipe Paulino (FlykeSpice)
/***************************************************************************

    gles2_rendersw.h

    Software renderer based on GLES2 for MAME4droid

***************************************************************************/

#pragma once 

#ifndef GLES2_RENDERSW_H
#define GLES2_RENDERSW_H

#include "myosd_renderer.h"
#include "emu/render.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <cstdlib>

class gles2_rendersw : public myosd_renderer
{
public:
	gles2_rendersw(int width, int height);

	void render(const render_primitive_list& primlist) override;
	void on_viewport_change(int width, int height) override;

	~gles2_rendersw() override {
		std::free(m_screenbuff);
		glDeleteTextures(1, &m_texture_id);
	}
private:
	GLuint m_program;
	GLuint m_texture_id;

	int m_width, m_height;
	int m_pitch;

	void* m_screenbuff;
};

#endif //GLES2_RENDERSW_H
