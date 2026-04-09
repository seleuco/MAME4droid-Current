// license:BSD-3-Clause
// copyright-holders:Filipe Paulino (FlykeSpice)
/***************************************************************************

    gles2_software.h

    Software renderer based on GLES2 for MAME4droid

***************************************************************************/

#pragma once 

#ifndef GLES2_SOFTWARE_H
#define GLES2_SOFTWARE_H

#include "myosd_renderer.h"
#include "emu/render.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <cstdlib>

class gles2_software : public myosd_renderer
{
public:
	gles2_software(int width, int height);

	void render(const render_primitive_list& primlist) override;
	void on_emulatedsize_change(int width, int height) override;

	//Shaders not supported by software renderer..
	void set_shader(const char* shader_name) override {}
	static std::vector<std::string> get_shaders_supported() { return {};}

	~gles2_software() override
	{
		std::free(m_screenbuff);
		glDeleteTextures(1, &m_texture_id);
		glDeleteProgram(m_program);
	}
private:
	GLuint m_program;
	GLuint m_texture_id;

	int m_width, m_height;
	int m_pitch;

	void* m_screenbuff;
};

#endif //GLES2_SOFTWARE_H
