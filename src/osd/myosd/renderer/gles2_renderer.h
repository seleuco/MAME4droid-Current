// license:BSD-3-Clause
// copyright-holders:Filipe Paulino (FlykeSpice)
/***************************************************************************

    gles2_renderer.h

    GLES2 renderer for MAME4droid

***************************************************************************/

#pragma once

#ifndef GLES2_RENDERER_H
#define GLES2_RENDERER_H

#include "myosd_renderer.h"

#include "filter_shader.h"

#include "osd/osdcore.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <array>
#include <cstdio>
#include <list>
#include <string>

typedef uintptr_t HashT;

class gles2_renderer : public myosd_renderer
{
public:
	gles2_renderer(int width, int height);

	void render(const render_primitive_list& primlist) override;

	void on_emulatedsize_change(int width, int height) override;

	void set_shader(const char* shader_name) override;
	static std::vector<std::string> get_shaders_supported();

	static void load_shaders(const std::string &root_path)
	{
		static bool initiated = false;

		if (!initiated)
		{
			s_filters = filter_shader::load_filters(root_path);
			initiated = true;
		}
	}

	struct gles2_texture
	{
		HashT hash;
		GLuint texture_id; //GLES2 texture object id
		render_texinfo texinfo; //Copy of the render_primitive texture info
		u32 prim_flags;  //Copy of the render_primitive flags
		osd_ticks_t last_access;

		void* base; //GL_ARGB format
		bool owned; //Do we own the raw data pointer, or is it a direct reference to textinfo.base?

		~gles2_texture()
		{
			glDeleteTextures(1, &texture_id);

			if (owned)
				std::free(base);
		}
	};

	//GL vertex attributes
	static constexpr GLuint ATTRIB_POSITION = 0;
	static constexpr GLuint ATTRIB_TEXUV    = 1;

	static constexpr u8 s_quad_indices[] = { 0, 1, 2, 0, 2, 3 }; //Indices to draw a quad with glDrawElements

	~gles2_renderer() override
	{
		glDeleteProgram(m_quad_program);
		glDeleteProgram(m_line_program);
	}

private:

	GLuint m_last_program = 0;
	void use_quad_program();
	void use_line_program();

	int m_last_blendmode = -1;
	void set_blendmode(int blendmode);

	void update_texture(const render_primitive& prim);
	gles2_texture* texture_find(const render_primitive& prim);
	void texture_create(const render_primitive& prim);

	//Shader program to render a quad primitive
	//each one deals with a specific texture format
	GLuint m_quad_program;
	GLint m_uniform_color_quad; //Primitive color for modulation
	GLint m_uniform_ortho_quad;

	GLuint m_line_program;
	GLint m_uniform_color_line; //Line solid color
	GLint m_uniform_ortho_line;

	std::array<float, 4*4> m_ortho; //Ortho projection matrix

	float m_quad_verts[4*2];
	float m_quad_uv[4*2];

	std::string m_lastfilter; //Last filter used
	bool m_usefilter = false; //If any filter is being used right now
	filter_shader m_filter; //Current filter shader used
	inline static std::map<std::string, std::string> s_filters = {};

	int m_width, m_height;
	
	std::list<gles2_texture> m_texlist; //Currently allocated textures
};

#endif //GLES2_RENDERER_H
