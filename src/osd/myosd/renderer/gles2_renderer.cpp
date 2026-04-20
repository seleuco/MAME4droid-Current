// license:BSD-3-Clause
// copyright-holders:Filipe Paulino (FlykeSpice) & Seleuco (David Valdeita)
/***************************************************************************

    gles2_renderer.cpp

    GLES2 renderer for MAME4droid

***************************************************************************/

#include "gles2_renderer.h"
#include "gl_utils.hxx"

#include "shader_sources.hxx"

#include "modules/render/copyutil.h"

#include <string>
#include <stdexcept>

using gles2_texture = gles2_renderer::gles2_texture;

//Prototypes
static HashT texture_compute_hash(const render_texinfo& texture, const u32 flags);
static void texture_copy_data(gles2_texture* texture, const render_texinfo& texinfo, u32 texformat);

void gles2_renderer::set_shader(const char* shader_name)
{
	if (shader_name)
	{
		if (m_lastfilter != shader_name)
		{
			m_filter.load_filter(s_filters[shader_name]);

			//Force program reload since loading filters changes it
			m_last_program = 0;

			m_lastfilter = shader_name;
		}
		m_usefilter = true;
	}
	else
	{
		m_usefilter = false;
	}
}

std::vector<std::string> gles2_renderer::get_shaders_supported()
{
	static std::vector<std::string> key_list;

	key_list.clear();
	for (const auto& [key, value] : s_filters)
		key_list.push_back(key);

	return key_list;
}

gles2_renderer::gles2_renderer(int width, int height)
{
	//First and foremost, let's check whether a shader compiler is supported.
	//Unfortunately, GLES 2 specification doesn't demand that every implementation bundle a shader compiler on the graphics driver
	GLboolean supported;
	glGetBooleanv(GL_SHADER_COMPILER, &supported);
	if (supported == GL_FALSE)
	{
		throw std::runtime_error("GLES2: Shader compilation isn't supported by your phone graphics driver");
	}

	//Disable some 3D stuff we don't use
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_POLYGON_OFFSET_FILL);

	glDisable(GL_BLEND);

	/* Init shader programs */

	GLuint quad_vertex_shader = gl_utils::load_shader(quad_vertex_shader_src, GL_VERTEX_SHADER);
	GLuint quad_frag_shader   = gl_utils::load_shader(quad_frag_shader_src,   GL_FRAGMENT_SHADER);
	m_quad_program = gl_utils::create_program(quad_vertex_shader, quad_frag_shader, {{ATTRIB_POSITION, "a_position"}, {ATTRIB_TEXUV, "a_texuv"}});

	GLuint line_vertex_shader = gl_utils::load_shader(line_vertex_shader_src, GL_VERTEX_SHADER);
	GLuint line_frag_shader   = gl_utils::load_shader(line_frag_shader_src, GL_FRAGMENT_SHADER);
	m_line_program = gl_utils::create_program(line_vertex_shader, line_frag_shader, {{ATTRIB_POSITION, "a_position"}});

	//Flag the shader objects for deletion, so they don't leak when the user is switching renderers
	glDeleteShader(quad_vertex_shader);
	glDeleteShader(quad_frag_shader);
	glDeleteShader(line_vertex_shader);
	glDeleteShader(line_frag_shader);

	glVertexAttribPointer(ATTRIB_POSITION, 2, GL_FLOAT, GL_TRUE, 0, m_quad_verts);
	glVertexAttribPointer(ATTRIB_TEXUV,    2, GL_FLOAT, GL_TRUE, 0, m_quad_uv);

	glEnableVertexAttribArray(ATTRIB_POSITION);
	glEnableVertexAttribArray(ATTRIB_TEXUV);

	//We're not gonna be compiling shaders anymore, release up the shader compiler resources
	glReleaseShaderCompiler();

	m_uniform_color_line = glGetUniformLocation(m_line_program, "u_color");
	m_uniform_color_quad = glGetUniformLocation(m_quad_program, "u_color");

	m_uniform_ortho_line = glGetUniformLocation(m_line_program, "u_ortho");
	m_uniform_ortho_quad = glGetUniformLocation(m_quad_program, "u_ortho");

	auto sampler_uniform = glGetUniformLocation(m_quad_program, "s_texture");
	glUniform1i(sampler_uniform, 0); //set sampler2D texture unit to 0

	on_emulatedsize_change(width, height);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

void gles2_renderer::on_emulatedsize_change(int width, int height)
{
	m_ortho = gl_utils::make_ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);
	m_width = width; m_height = height;

	//Force program reupload to update ortho matrix uniform
	m_last_program = 0;

    m_force_viewport_update = true;

	m_filter.set_ortho(m_ortho);
}

void gles2_renderer::use_quad_program()
{
	//Use quad shader program object and enable the quad vertex attrib
	if (m_last_program != m_quad_program)
	{
		glUseProgram(m_quad_program);
		glUniformMatrix4fv(m_uniform_ortho_quad, 1, GL_FALSE, m_ortho.data());
		m_last_program = m_quad_program;
	}

}

void gles2_renderer::use_line_program()
{
	//Use line shader
	if (m_last_program != m_line_program)
	{
		glUseProgram(m_line_program);
		glUniformMatrix4fv(m_uniform_ortho_line, 1, GL_FALSE, m_ortho.data());
		m_last_program = m_line_program;
	}
}

//copied from osd/modules/drawogl.cpp
void gles2_renderer::set_blendmode(int blendmode)
{
	// try to minimize texture state changes
	if (blendmode != m_last_blendmode)
	{
		switch (blendmode)
		{
			case BLENDMODE_NONE:
				glDisable(GL_BLEND);
				break;
			case BLENDMODE_ALPHA:
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				break;
			case BLENDMODE_RGB_MULTIPLY:
				glEnable(GL_BLEND);
				glBlendFunc(GL_DST_COLOR, GL_ZERO);
				break;
			case BLENDMODE_ADD:
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE);
				break;
		}

		m_last_blendmode = blendmode;
	}
}

void gles2_renderer::render(const render_primitive_list& primlist)
{
    if (m_force_viewport_update)
    {
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);

        // Failsafe: Only store the values if OpenGL returns logical physical dimensions (> 0)
        if (viewport[2] > 0 && viewport[3] > 0)
        {
            m_view_width = viewport[2];
            m_view_height = viewport[3];

            m_force_viewport_update = false; // Successfully updated, clear the flag
        }
    }

	//TODO: Batch many primitives that share the same properties (format, colors..) into a single draw call
	for (const render_primitive& prim : primlist)
	{
		switch (prim.type)
		{
			case render_primitive::LINE:
			{
				use_line_program();

				const render_bounds& bounds = prim.bounds;
				//Eeh not a quad, but we reuse the attrib on the line shader
				m_quad_verts[0] = bounds.x0;
				m_quad_verts[1] = bounds.y0;

				m_quad_verts[2] = bounds.x1;
				m_quad_verts[3] = bounds.y1;

				set_blendmode(PRIMFLAG_GET_BLENDMODE(prim.flags));

				glUniform4f(m_uniform_color_line, prim.color.r, prim.color.g, prim.color.b, prim.color.a);

				glDrawArrays(GL_LINES, 0, 2);
			}
			break;

			case render_primitive::QUAD:
			{
				bool has_texture = prim.texture.base != nullptr;
				if (has_texture)
				{
					use_quad_program();
					update_texture(prim);
				}
				else
				{
					//For drawing just the solid colors
					use_line_program();
				}

				glUniform4f(has_texture ? m_uniform_color_quad : m_uniform_color_line, prim.color.r, prim.color.g, prim.color.b, prim.color.a);

				const render_bounds& bounds = prim.bounds;
				m_quad_verts[0] = bounds.x0;
				m_quad_verts[1] = bounds.y0;

				m_quad_verts[2] = bounds.x0;
				m_quad_verts[3] = bounds.y1;

				m_quad_verts[4] = bounds.x1;
				m_quad_verts[5] = bounds.y1;

				m_quad_verts[6] = bounds.x1;
				m_quad_verts[7] = bounds.y0;

				if (has_texture)
				{
					const render_quad_texuv& texuv = prim.texcoords;
					m_quad_uv[0] = texuv.tl.u;
					m_quad_uv[1] = texuv.tl.v;

					m_quad_uv[2] = texuv.bl.u;
					m_quad_uv[3] = texuv.bl.v;

					m_quad_uv[4] = texuv.br.u;
					m_quad_uv[5] = texuv.br.v;

					m_quad_uv[6] = texuv.tr.u;
					m_quad_uv[7] = texuv.tr.v;
				}

				set_blendmode(PRIMFLAG_GET_BLENDMODE(prim.flags));

				const bool usefilter = m_usefilter && PRIMFLAG_GET_SCREENTEX(prim.flags);
				if (usefilter)
				{
					//m_filter.draw(prim.get_quad_width(), prim.get_quad_height());
                    // This ensures that resolution-dependent shaders (like Mattias CRT) scale their
                    // effects correctly relative to the actual display area rather than the emulated quad.
                    m_filter.draw(m_view_width, m_view_height);
					m_last_program = 0; //Restore to previous program
				}
				else
				{
					glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, s_quad_indices);
				}

			}
			break;

			case render_primitive::INVALID:
			//FlykeSpice: throw? or do nothing
			break;
		}
	}

}

static void texture_copy_data(gles2_texture* texture, const render_texinfo& texinfo, u32 texformat)
{
	for (int y=0; y<texinfo.height; y++)
	{
		uint32_t *dst = (u32*)texture->base + (texinfo.width * y);

		#define src(T) (T*)texinfo.base + (texinfo.rowpixels * y)

		switch (texformat)
		{
			case TEXFORMAT_RGB32:
				copy_util::copyline_rgb32(dst, src(u32), texinfo.width, texinfo.palette);
				break;
			case TEXFORMAT_ARGB32:
				copy_util::copyline_argb32(dst, src(u32), texinfo.width, texinfo.palette);
				break;
			case TEXFORMAT_PALETTE16:
				copy_util::copyline_palette16(dst, src(u16), texinfo.width, texinfo.palette);
				break;
			case TEXFORMAT_YUY16:
				//TODO: If the YUV16 texture isn't paletted, we can just do the texel conversion on fragment shader...
				copy_util::copyline_yuy16_to_argb(dst, src(u16), texinfo.width, texinfo.palette, 1);
				break;
		}
		#undef src
	}
}

void gles2_renderer::update_texture(const render_primitive& prim)
{
	const render_texinfo& texinfo = prim.texture;
	gles2_texture* texture = texture_find(prim);

	if (texture == nullptr)
		texture_create(prim);
	else
	{
		//TODO: We found a previous allocated texture with the same dimensions, but did the pixel data change?
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture->texture_id);

		//I don't know why but MAME osd render implementations check whether the texture data has changed by checking if 'seqid' value changed...
		if (texture->texinfo.seqid != prim.texture.seqid)
		{
			texture->texinfo.seqid = prim.texture.seqid;
			texture_copy_data(texture, texinfo, PRIMFLAG_GET_TEXFORMAT(prim.flags));

			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texture->texinfo.width, texture->texinfo.height, GL_RGBA, GL_UNSIGNED_BYTE, texture->base);
		}
	}
}

void gles2_renderer::texture_create(const render_primitive& prim)
{
	const render_texinfo& texinfo = prim.texture;
	gles2_texture& texture = m_texlist.emplace_front();

	texture.hash = texture_compute_hash(texinfo, prim.flags);

	if (PRIMFLAG_GET_SCREENTEX(prim.flags))
	{
		m_filter.set_input_size(texinfo.width, texinfo.height);
	}

	glGenTextures(1, &texture.texture_id);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture.texture_id);

	texture.texinfo = texinfo;
	texture.prim_flags = prim.flags;

	const auto texformat = PRIMFLAG_GET_TEXFORMAT(prim.flags);

	//Unfortunately, quad primitive textures often have strides (row padding) and OpenGLES 2.0 dont have support for those... so we need to manually copy all textures data
	//should we jump to directly to GLES3? (which added strides support)
#if 0
	if ((texformat == TEXFORMAT_RGB32 || texformat == TEXFORMAT_ARGB32) && texinfo.palette == nullptr)
	{
		//RGB(A) mapping in the texture is direct, no need to copy over
		texture.base = texinfo.base;
		texture.owned = false; //We don't own the data (FIXME: we probably don't need this and can just keep texure.base nullptr and reference texinfo.base directly...)
	}
	else
#endif
	{
		//We need to copy over
		texture.base = std::malloc((texinfo.width*4)*texinfo.height); //FIXME: Use an allocated memory pool rather than allocating every frame...
		texture.owned = true;

		texture_copy_data(&texture, texinfo, texformat);
	}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texinfo.width, texinfo.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture.base);

	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    //smoothing
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	GLint wrapmode = PRIMFLAG_GET_TEXWRAP(prim.flags) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapmode);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapmode);

	//glBindTexture(GL_TEXTURE_2D, 0);

	texture.last_access = osd_ticks();
}

//=========================================================
// Texture hashing utilities
//=========================================================

//Copy-pasted from osd/module/render/drawsdl3accel.cpp
static inline HashT texture_compute_hash(const render_texinfo &texture, const u32 flags)
{
	return (HashT)texture.base ^ (flags & (PRIMFLAG_BLENDMODE_MASK | PRIMFLAG_TEXFORMAT_MASK));
}

static bool compare_texture_primitive(const gles2_texture& texture, const render_primitive& prim)
{
	//Just compare if the dimensions are the same, we can update the pixel data if they changed
	return texture.texinfo.base == prim.texture.base
		&& texture.texinfo.width     == prim.texture.width
		&& texture.texinfo.height    == prim.texture.height
		&& texture.texinfo.rowpixels == prim.texture.rowpixels
		&& texture.texinfo.palette   == prim.texture.palette
		&& ((texture.prim_flags ^ prim.flags) & (PRIMFLAG_BLENDMODE_MASK | PRIMFLAG_TEXFORMAT_MASK)) == 0;
}

gles2_texture* gles2_renderer::texture_find(const render_primitive& prim)
{
	//Check if we have the texture cached by computing its hash with ours
	//const HashT hash = texture_compute_hash(prim.texture, prim.flags);
	const osd_ticks_t now = osd_ticks();

	for (auto texture = m_texlist.begin(); texture != m_texlist.end(); )
	{
		if (compare_texture_primitive(*texture, prim))
		{
			texture->last_access = now;
			return &*texture;
		}
		else
		{
			//TODO: Better offloading this to a background thread that occasionally does cleanup of unused textures?
			if ((now - texture->last_access) > osd_ticks_per_second())
				texture = m_texlist.erase(texture);
			else
				++texture;
		}
	}

	return nullptr;
}
