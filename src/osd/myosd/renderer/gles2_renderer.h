// license:BSD-3-Clause
// copyright-holders:Filipe Paulino (FlykeSpice)
/***************************************************************************

    gles2_renderer.h

    GLES2 renderer for MAME4droid

***************************************************************************/

#ifndef GLES2_RENDERER_H
#define GLES2_RENDERER_H

#pragma once

#include "emu/emucore.h"
#include "emu/render.h"
#include "osd/osdcore.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <cstdio>
#include <list>

typedef uintptr_t HashT;

class gles2_renderer
{
public:
	gles2_renderer(int width, int height);

	void render(const render_primitive_list& primlist);
	void on_viewport_change(int width, int height);

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

private:
	GLuint create_program(GLuint vertex_shader, GLuint frag_shader);

	GLuint m_last_program;
	void use_quad_program();
	void use_line_program();

	int m_last_blendmode;
	void set_blendmode(int blendmode);

	void update_texture(const render_primitive& prim);
	gles2_texture* texture_find(const render_primitive& prim);
	void texture_create(const render_primitive& prim);

	//Shader program to render a quad primitive
	//each one deals with a specific texture format
	GLuint m_quad_program;
	GLint m_uniform_color_quad; //Primitive color for modulation

	GLuint m_line_program;
	GLint m_uniform_color_line; //Line solid color

	//GL vertex attributes
	static const GLuint m_attrib_position = 0;
	static const GLuint m_attrib_texuv    = 1;

	float m_quad_verts[4*2];
	float m_quad_uv[4*2];
	static constexpr u8 m_quad_indices[] = { 0, 1, 2, 0, 2, 3 }; //Indices to draw a quad with glDrawElements
	
	std::list<gles2_texture> m_texlist; //Currently allocated textures
};

#endif //GLES2_RENDERER_H
