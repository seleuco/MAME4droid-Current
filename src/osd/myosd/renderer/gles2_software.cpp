// license:BSD-3-Clause
// copyright-holders:Filipe Paulino (FlykeSpice)
/***************************************************************************

    gles2_software.cpp

    Software renderer based on GLES2 for MAME4droid

***************************************************************************/


#include "gles2_software.h"
#include "gl_utils.hxx"
#include "emu/rendersw.hxx"

#include <stdexcept>

static const char* vert_src = R"(
attribute vec2 a_position;
varying vec2 v_uv;

void main()
{
	gl_Position = vec4(a_position, 0, 1.0);
	v_uv = a_position * 0.5 + vec2(0.5);
	v_uv.y = 1.0 - v_uv.y;
}
)";

static const char* frag_src = R"(
varying vec2 v_uv;
uniform sampler2D s_texture;

void main()
{
	gl_FragColor = vec4(texture2D(s_texture, v_uv).rgb, 1.0);
}
)";

gles2_software::gles2_software(int width, int height)
	: m_screenbuff(nullptr)
{
        //Disable some 3D stuff we don't use
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_BLEND);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	auto vert_shader = gl_utils::load_shader(vert_src, GL_VERTEX_SHADER);
	auto frag_shader = gl_utils::load_shader(frag_src, GL_FRAGMENT_SHADER);
	m_program = gl_utils::create_program(vert_shader, frag_shader);
	glUseProgram(m_program);

	glDeleteShader(vert_shader);
	glDeleteShader(frag_shader);

	auto attrib_position = glGetAttribLocation(m_program, "a_position");
	if (attrib_position == -1)
		throw std::runtime_error("GLES2 Software: unable to retrieve attribute a_position location");

	static const GLfloat verts[] =
	{
		-1.0f, -1.0f,
		3.0f, -1.0f,
		-1.0f, 3.0f
	};
	glVertexAttribPointer(attrib_position, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glEnableVertexAttribArray(attrib_position);

	glActiveTexture(GL_TEXTURE0);
	glGenTextures(1, &m_texture_id);
	glBindTexture(GL_TEXTURE_2D, m_texture_id);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	auto sampler_uniform = glGetUniformLocation(m_program, "s_texture");
	glUniform1i(sampler_uniform, 0); //set sampler2D texture unit to 0

	on_emulatedsize_change(width, height);
}

void gles2_software::render(const render_primitive_list& primlist)
{
	software_renderer<uint32_t, 0, 0, 0, 0, 8, 16>::draw_primitives(primlist, m_screenbuff, m_width, m_height, m_pitch);

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, m_screenbuff);

	//Draw the screen-covering texture using the fullscreen triangle optimization
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

void gles2_software::on_emulatedsize_change(int width, int height)
{
	m_width = width;
	m_height = height;

	m_pitch = width;

	m_screenbuff = std::realloc(m_screenbuff, m_pitch*height*4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	glViewport(0, 0, width, height);
}
