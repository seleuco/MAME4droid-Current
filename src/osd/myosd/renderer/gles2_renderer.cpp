// license:BSD-3-Clause
// copyright-holders:Filipe Paulino (FlykeSpice) & David Valdeita (Seleuco)
/***************************************************************************

    gles2_renderer.cpp

    GL MAME Native renderer based on GLES 2.x for MAME4droid

***************************************************************************/

#include "gles2_renderer.h"
#include "gl_utils.hxx"

#include "shader_sources.hxx"

#include "modules/render/copyutil.h"

#include <android/log.h>

#include <string>
#include <stdexcept>
#include <algorithm>

#define ANDROID_LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "gles2_renderer", __VA_ARGS__)

using gles2_texture = gles2_renderer::gles2_texture;

//Prototypes
static HashT texture_compute_hash(const render_texinfo& texture, const u32 flags);
static void texture_copy_data(void* dest, const render_texinfo& texinfo, u32 texformat);
static bool compare_texture_primitive(const gles2_texture& texture, const render_primitive& prim);

void gles2_renderer::set_shader(const char* shader_name)
{
    ANDROID_LOG("set_shader %s...", shader_name);
    if (shader_name)
    {
        if (m_lastfilter != shader_name)
        {
            auto it = std::find_if(s_filters.begin(), s_filters.end(),
                                   [&](const std::pair<std::string, filter_data>& p) { return p.first == shader_name; });

            if (it != s_filters.end())
            {
                m_filter.load_filter(it->second.source, it->second.linear);
                m_last_program = 0;
                m_lastfilter = shader_name;
            }
            else
            {
                ANDROID_LOG("shader not found!");
                return;
            }
        }
        m_usefilter = true;
    }
    else
    {
        if(m_lastfilter.empty())
        {
            m_last_program = 0;
        }
        m_lastfilter = "";
        m_usefilter = false;
    }
}

std::vector<std::string> gles2_renderer::get_shaders_supported()
{
    std::vector<std::string> key_list;
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

void gles2_renderer::end_renderer()
{
    m_flush_textures = true;
}

void gles2_renderer::on_emulatedsize_change(int width, int height)
{
    std::lock_guard<std::mutex> lock(m_render_mutex);	
	
	m_ortho = gl_utils::make_ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);
	m_width = width; m_height = height;

    m_last_filter_mode = myosd_get(MYOSD_BITMAP_FILTERING);

    m_force_viewport_update = true;

    m_flush_textures = true;

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

void gles2_renderer::sync_state(const render_primitive_list* primlist)
{
    if (m_flush_textures || m_last_filter_mode != myosd_get(MYOSD_BITMAP_FILTERING))
    {
        m_last_filter_mode = myosd_get(MYOSD_BITMAP_FILTERING);
		for (auto& tex : m_texlist) {
            if (tex->texture_id != 0) m_textures_to_delete.push_back(tex->texture_id);
        }		
        m_texlist.clear();
        m_flush_textures = false;
    }
	
	std::vector<local_primitive> temp_prims;

    // Deep copy
    for (const render_primitive& prim : *primlist)
    {
        local_primitive lp;
        lp.type = prim.type;
        lp.bounds = prim.bounds;
        lp.color = prim.color;
        lp.texcoords = prim.texcoords;
        lp.flags = prim.flags;
        lp.texture = nullptr;

        if (prim.type == render_primitive::QUAD && prim.texture.base != nullptr)
        {
            update_texture_cache(prim, lp.texture);
        }
		temp_prims.push_back(lp);				
    }
	
	{
		std::lock_guard<std::mutex> lock(m_render_mutex);
        
		for (auto& lp : temp_prims) {
            if (lp.texture && lp.texture->needs_gl_update) {

                std::swap(lp.texture->base, lp.texture->base_back);                
                lp.needs_texture_upload = true;                
                lp.texture->needs_gl_update = false; 
            }
        }	

        m_render_prims = std::move(temp_prims);
        
        m_render_textures_to_delete.insert(m_render_textures_to_delete.end(), 
                                           m_textures_to_delete.begin(), 
                                           m_textures_to_delete.end());
        m_textures_to_delete.clear();
    }	
	
}

void gles2_renderer::render()
{
	std::vector<local_primitive> draw_prims;
    std::vector<GLuint> delete_texs;

    {
        std::lock_guard<std::mutex> lock(m_render_mutex);
        draw_prims = m_render_prims; // Copia ligera (solo copia punteros y valores)
        delete_texs = std::move(m_render_textures_to_delete);
        m_render_textures_to_delete.clear();
    }

	glClear(GL_COLOR_BUFFER_BIT);//if not trash if use sliders to change osd position

    if (m_force_viewport_update)
    {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	    //glClearColor(0.0f, 0.0f, 1.0f, 1.0f);

		GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);

        // Failsafe: Only store the values if OpenGL returns logical physical dimensions (> 0)
        if (viewport[2] > 0 && viewport[3] > 0)
        {
            m_view_width = viewport[2];
            m_view_height = viewport[3];
			
			ANDROID_LOG("viewport %d %d",m_view_width, m_view_height);

            m_force_viewport_update = false; // Successfully updated, clear the flag
        }
    }

	//TODO: Batch many primitives that share the same properties (format, colors..) into a single draw call

	for (const local_primitive& prim : draw_prims)
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
				bool has_texture = prim.texture != nullptr;
				if (has_texture)
				{
					use_quad_program();

					if (prim.texture->needs_gl_init) {
                        glGenTextures(1, &prim.texture->texture_id);
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, prim.texture->texture_id);

                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, prim.texture->texinfo.width, prim.texture->texinfo.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, prim.texture->base);

                        GLint filter_mode = myosd_get(MYOSD_BITMAP_FILTERING) ? GL_LINEAR : GL_NEAREST;
                        if (PRIMFLAG_GET_SCREENTEX(prim.flags) && m_usefilter) {
                            filter_mode = m_filter.is_linear() ? GL_LINEAR : GL_NEAREST;
                        }

                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter_mode);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_mode);

                        GLint wrapmode = PRIMFLAG_GET_TEXWRAP(prim.flags) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapmode);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapmode);

                        prim.texture->needs_gl_init = false;
                    }
                    else
                    {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, prim.texture->texture_id);

						if (prim.needs_texture_upload) { 
                            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, prim.texture->texinfo.width, prim.texture->texinfo.height, GL_RGBA, GL_UNSIGNED_BYTE, prim.texture->base);
                        }
                    }
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
                    // WARNING: Ensure no EBO is bound here, as s_quad_indices is a client-side pointer.
                    //glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
					glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, s_quad_indices);
				}

			}
			break;

			case render_primitive::INVALID:
			//FlykeSpice: throw? or do nothing
			break;
		}
	}
	
	if (!delete_texs.empty()) {
        glDeleteTextures(delete_texs.size(), delete_texs.data());
    }
}

static void texture_copy_data(void* dest, const render_texinfo& texinfo, u32 texformat)
{
	for (int y=0; y<texinfo.height; y++)
	{
		uint32_t *dst = (u32*)dest + (texinfo.width * y);

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

void gles2_renderer::update_texture_cache(const render_primitive& prim, std::shared_ptr<gles2_texture>& out_tex)
{
	std::shared_ptr<gles2_texture> texture = texture_find(prim, osd_ticks());

	if (texture == nullptr) {
		out_tex = texture_create(prim);
    }
	else
	{
		if (texture->texinfo.seqid != prim.texture.seqid)
		{
			texture->texinfo.seqid = prim.texture.seqid;
			texture_copy_data(texture->base_back, prim.texture, PRIMFLAG_GET_TEXFORMAT(prim.flags));
            texture->needs_gl_update = true;
		}
        out_tex = texture;
	}
}

std::shared_ptr<gles2_renderer::gles2_texture> gles2_renderer::texture_create(const render_primitive& prim)
{
	const render_texinfo& texinfo = prim.texture;
	
    std::shared_ptr<gles2_texture> texture = std::make_shared<gles2_texture>();
	m_texlist.push_front(texture);

	texture->hash = texture_compute_hash(texinfo, prim.flags);

	if (PRIMFLAG_GET_SCREENTEX(prim.flags))
	{
		m_filter.set_input_size(texinfo.width, texinfo.height);
	}

	texture->texinfo = texinfo;
	texture->prim_flags = prim.flags;

	const auto texformat = PRIMFLAG_GET_TEXFORMAT(prim.flags);

	texture->base = std::malloc((texinfo.width*4)*texinfo.height);
	texture->base_back = std::malloc((texinfo.width*4)*texinfo.height);
	texture->owned = true;

	texture_copy_data(texture->base, texinfo, texformat);

    texture->needs_gl_init = true;
	texture->last_access = osd_ticks();

	return texture;
}

std::shared_ptr<gles2_renderer::gles2_texture> gles2_renderer::texture_find(const render_primitive& prim, osd_ticks_t now)
{
	for (auto it = m_texlist.begin(); it != m_texlist.end(); )
	{
		if (compare_texture_primitive(**it, prim))
		{
			(*it)->last_access = now;
			return *it;
		}
		else
		{
			if ((now - (*it)->last_access) > osd_ticks_per_second() )
			{
				if ((*it)->texture_id > 0) {
				    m_textures_to_delete.push_back((*it)->texture_id);
                }				
				it = m_texlist.erase(it);
			}
			else
				++it;
		}
	}

	return nullptr;
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

