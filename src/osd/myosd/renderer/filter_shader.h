// license:BSD-3-Clause
// copyright-holders:Filipe Paulino (FlykeSpice)
/***************************************************************************

    filter_shader.h

    Effect shaders for MAME4droid's GLES2 renderer

***************************************************************************/

#pragma once

#ifndef MAME4DROID_FILTERSHADER
#define MAME4DROID_FILTERSHADER

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <array>
#include <map>
#include <string>

/*
 * Encapsulates a single-screen filter GLES 2 shader for overlay effects
 */
class filter_shader
{
public:
	void load_filter(const std::string &filter_src);

	void set_input_size(int width, int height);
	void set_ortho(std::array<float, 4*4> ortho);

	void draw(int width, int height);

	static std::map<std::string, std::string> load_filters(const std::string &root_path);

	~filter_shader()
	{
		glDeleteProgram(m_program);
	}

private:
	//shader program
	GLuint m_program = 0;

	GLint m_uniform_MVPMatrix;

	GLint m_uniform_InputSize;
	GLint m_uniform_OutputSize;
	GLint m_uniform_TextureSize;

	GLint m_uniform_FrameCount;
	unsigned m_framecount;

	int m_texwidth = 0, m_texheight = 0;
	std::array<float, 4*4> m_ortho;
};

#endif //MAME4DROID_FILTERSHADER
