// license:BSD-3-Clause
// copyright-holders: David Valdeita (Seleuco) & Filipe Paulino (FlykeSpice)
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
#include <cmath>
#include <utility>

#define ANDROID_LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "gles2_renderer", __VA_ARGS__)

struct line_aa_step {
	float xoffs, yoffs;
	float weight;
};

static const line_aa_step line_aa_1step[] = {
	{  0.00f,  0.00f,  1.00f },
	{ 0 }
};

static const line_aa_step line_aa_4step[] = {
	{ -0.25f,  0.00f,  0.25f },
	{  0.25f,  0.00f,  0.25f },
	{  0.00f, -0.25f,  0.25f },
	{  0.00f,  0.25f,  0.25f },
	{ 0 }
};

using gles2_texture = gles2_renderer::gles2_texture;

static std::pair<render_bounds, render_bounds> render_line_to_quad(const render_bounds& bounds, float width, float extension)
{
    render_bounds b0, b1;
    float dx = bounds.x1 - bounds.x0;
    float dy = bounds.y1 - bounds.y0;
    float length = std::sqrt(dx * dx + dy * dy);

    if (length > 0.0001f)
    {
        float half_width = width * 0.5f;
        float nx = -dy / length * half_width;
        float ny =  dx / length * half_width;

        b0.x0 = bounds.x0 + nx;  b0.y0 = bounds.y0 + ny;
        b0.x1 = bounds.x0 - nx;  b0.y1 = bounds.y0 - ny;

        b1.x0 = bounds.x1 + nx;  b1.y0 = bounds.y1 + ny;
        b1.x1 = bounds.x1 - nx;  b1.y1 = bounds.y1 - ny;
    }
    else
    {
        b0.x0 = b0.x1 = bounds.x0; b0.y0 = b0.y1 = bounds.y0;
        b1.x0 = b1.x1 = bounds.x1; b1.y0 = b1.y1 = bounds.y1;
    }
    return std::make_pair(b0, b1);
}

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
	/* Init quad shader program*/
	GLuint quad_vertex_shader = gl_utils::load_shader(quad_vertex_shader_src, GL_VERTEX_SHADER);
	GLuint quad_frag_shader   = gl_utils::load_shader(quad_frag_shader_src,   GL_FRAGMENT_SHADER);
	m_quad_program = gl_utils::create_program(quad_vertex_shader, quad_frag_shader, {{ATTRIB_POSITION, "a_position"}, {ATTRIB_TEXUV, "a_texuv"}, {ATTRIB_COLOR, "a_color"}});

	//Flag the shader objects for deletion, so they don't leak when the user is switching renderers
	glDeleteShader(quad_vertex_shader);
	glDeleteShader(quad_frag_shader);

	//We're not gonna be compiling shaders anymore, release up the shader compiler resources
	glReleaseShaderCompiler();

	m_uniform_ortho_quad = glGetUniformLocation(m_quad_program, "u_ortho");

	auto sampler_uniform = glGetUniformLocation(m_quad_program, "s_texture");
	glUniform1i(sampler_uniform, 0); //set sampler2D texture unit to 0
	
	glGenTextures(1, &m_white_texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_white_texture);
	uint32_t white_pixel = 0xFFFFFFFF; // RGBA white
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white_pixel);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &m_glow_texture);
	glBindTexture(GL_TEXTURE_2D, m_glow_texture);
	uint32_t glow_pixels[64];
	for (int i = 0; i < 64; i++) {
		float dist = std::fabs((i - 31.5f) / 31.5f);
		float intensity = 1.0f - (dist * dist); 
		if (intensity < 0.0f) intensity = 0.0f;
		uint8_t a = (uint8_t)(intensity * 255.0f);
		glow_pixels[i] = (a << 24) | 0x00FFFFFF; 
	}
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, glow_pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);	
	
	m_batch_vertices.reserve(4096); 
	m_batch_indices.reserve(6144);
	
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	on_emulatedsize_change(width, height);
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
	
	m_fbo_dirty = true;
}

void gles2_renderer::create_fbo(int width, int height) {
    delete_fbo();
    glGenTextures(1, &m_fbo_texture);
    glBindTexture(GL_TEXTURE_2D, m_fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fbo_texture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void gles2_renderer::delete_fbo() {
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    if (m_fbo_texture) glDeleteTextures(1, &m_fbo_texture);
    m_fbo = 0; m_fbo_texture = 0;
}


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
	
	//clean old textures
	osd_ticks_t now = osd_ticks();
	for (auto it = m_texlist.begin(); it != m_texlist.end(); )
	{
		if ((now - (*it)->last_access) > osd_ticks_per_second())
		{
			if ((*it)->texture_id > 0) {
				m_textures_to_delete.push_back((*it)->texture_id);
			}				
			it = m_texlist.erase(it);
	}	
		else
		{
			++it;
		}
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
		lp.width = prim.width;
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
			if (lp.texture) {
			
				if (lp.texture->needs_gl_update) {
					std::swap(lp.texture->base, lp.texture->base_back);                
					lp.needs_texture_upload = true;
					lp.texture->needs_gl_update = false; 
				}

				lp.upload_ptr = lp.texture->base; 
			}
        }	

        m_render_prims = std::move(temp_prims);
        
        m_render_textures_to_delete.insert(m_render_textures_to_delete.end(), 
                                           m_textures_to_delete.begin(), 
                                           m_textures_to_delete.end());
        m_textures_to_delete.clear();
/*		
		std::string traza = "";
		int estaticas = 0, dinamicas = 0;

		for (const auto& tex : m_texlist) {
			bool es_dinamica = (tex->base_back != nullptr);
			es_dinamica ? dinamicas++ : estaticas++;
			char buf[64];
			snprintf(buf, sizeof(buf), "[ID:%u %dx%d %s]", 
					 tex->texture_id, tex->texinfo.width, tex->texinfo.height, 
					 es_dinamica ? "DIN" : "EST");
			traza += buf;
    }	
		ANDROID_LOG("CACHE TOTAL -> Elementos: %zu (Estaticas: %d | Dinamicas: %d) Info: %s", m_texlist.size(), estaticas, dinamicas, traza.c_str());
*/
	}

}

void gles2_renderer::push_quad(const float* verts, const float* uv, const render_color& color) 
{
	if (m_batch_vertices.size() >= 65000) {
        flush_batch();
    }
	
    GLushort base = m_batch_vertices.size();
    
    static const float default_uv[8] = {0.0f};
    const float* actual_uv = uv ? uv : default_uv;
    
    m_batch_vertices.push_back({verts[0], verts[1], actual_uv[0], actual_uv[1], color.r, color.g, color.b, color.a});
    m_batch_vertices.push_back({verts[2], verts[3], actual_uv[2], actual_uv[3], color.r, color.g, color.b, color.a});
    m_batch_vertices.push_back({verts[4], verts[5], actual_uv[4], actual_uv[5], color.r, color.g, color.b, color.a});
    m_batch_vertices.push_back({verts[6], verts[7], actual_uv[6], actual_uv[7], color.r, color.g, color.b, color.a});

    m_batch_indices.push_back(base + 0); m_batch_indices.push_back(base + 1); m_batch_indices.push_back(base + 2);
    m_batch_indices.push_back(base + 0); m_batch_indices.push_back(base + 2); m_batch_indices.push_back(base + 3);
}

void gles2_renderer::flush_batch() 
{
    if (m_batch_indices.empty()) return;
	
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    int stride = sizeof(vertex_data);
    const void* pointer_pos = (const void*)offsetof(vertex_data, x);
    const void* pointer_uv  = (const void*)offsetof(vertex_data, u);
    const void* pointer_col = (const void*)offsetof(vertex_data, r);

    glVertexAttribPointer(ATTRIB_POSITION, 2, GL_FLOAT, GL_FALSE, stride, (const uint8_t*)m_batch_vertices.data() + (size_t)pointer_pos);
    glEnableVertexAttribArray(ATTRIB_POSITION);

    glVertexAttribPointer(ATTRIB_TEXUV, 2, GL_FLOAT, GL_FALSE, stride, (const uint8_t*)m_batch_vertices.data() + (size_t)pointer_uv);
    glEnableVertexAttribArray(ATTRIB_TEXUV);

    glVertexAttribPointer(ATTRIB_COLOR, 4, GL_FLOAT, GL_FALSE, stride, (const uint8_t*)m_batch_vertices.data() + (size_t)pointer_col);
    glEnableVertexAttribArray(ATTRIB_COLOR);

    glDrawElements(GL_TRIANGLES, m_batch_indices.size(), GL_UNSIGNED_SHORT, m_batch_indices.data());

    m_batch_vertices.clear();
    m_batch_indices.clear();
}

void gles2_renderer::render()
{
	std::vector<local_primitive> draw_prims;
    std::vector<GLuint> delete_texs;

    {
        std::lock_guard<std::mutex> lock(m_render_mutex);
        draw_prims = m_render_prims; 
        delete_texs = std::move(m_render_textures_to_delete);
        m_render_textures_to_delete.clear();
    }
	
	if (m_usefilter && m_fbo_dirty) {
        create_fbo(m_width, m_height);
        m_fbo_dirty = false;
    }
	
    render_bounds v_bounds = { 99999.0f, 99999.0f, -99999.0f, -99999.0f };
    bool has_vectors = false;
    for (const local_primitive& prim : draw_prims) {
        if (PRIMFLAG_GET_VECTOR(prim.flags)) {
            float min_x = std::min(prim.bounds.x0, prim.bounds.x1); float max_x = std::max(prim.bounds.x0, prim.bounds.x1);
            float min_y = std::min(prim.bounds.y0, prim.bounds.y1); float max_y = std::max(prim.bounds.y0, prim.bounds.y1);
            v_bounds.x0 = std::min(v_bounds.x0, min_x); v_bounds.y0 = std::min(v_bounds.y0, min_y);
            v_bounds.x1 = std::max(v_bounds.x1, max_x); v_bounds.y1 = std::max(v_bounds.y1, max_y);
            has_vectors = true;
        }
    }

    if (has_vectors) {
        v_bounds.x0 = std::max(0.0f, v_bounds.x0 - 15.0f); v_bounds.y0 = std::max(0.0f, v_bounds.y0 - 15.0f);
        v_bounds.x1 = std::min((float)m_width, v_bounds.x1 + 15.0f); v_bounds.y1 = std::min((float)m_height, v_bounds.y1 + 15.0f);
    }

    GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    if (m_force_viewport_update)
    {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

		GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);

        if (viewport[2] > 0 && viewport[3] > 0)
        {
            m_view_width = viewport[2];
            m_view_height = viewport[3];
            m_force_viewport_update = false;
        }
    }

	for (local_primitive& prim : draw_prims)
	{
		if (prim.texture && (prim.texture->needs_gl_init || prim.needs_texture_upload))
		{
			if (prim.texture->needs_gl_init) {
				glGenTextures(1, &prim.texture->texture_id);
				glBindTexture(GL_TEXTURE_2D, prim.texture->texture_id);

				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, prim.texture->texinfo.width, prim.texture->texinfo.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, prim.upload_ptr);
				
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
			else if (prim.needs_texture_upload) {
				glBindTexture(GL_TEXTURE_2D, prim.texture->texture_id);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, prim.texture->texinfo.width, prim.texture->texinfo.height, GL_RGBA, GL_UNSIGNED_BYTE, prim.upload_ptr);
			}
			
			prim.needs_texture_upload = false; 
		}
	}

	glUseProgram(m_quad_program);
	glUniformMatrix4fv(m_uniform_ortho_quad, 1, GL_FALSE, m_ortho.data());

	m_current_texture = 0; 
    m_last_blendmode = -1; 
	
    bool enable_bloom = true;
	bool fbo_active = false;

    auto draw_vector_fbo = [&]() {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);

        float u0 = v_bounds.x0 / m_width; float u1 = v_bounds.x1 / m_width;
        float v0 = 1.0f - (v_bounds.y0 / m_height); float v1 = 1.0f - (v_bounds.y1 / m_height);

        float fbo_verts[8] = { v_bounds.x0, v_bounds.y0, v_bounds.x0, v_bounds.y1, v_bounds.x1, v_bounds.y1, v_bounds.x1, v_bounds.y0 };
        float fbo_uv[8] = { u0, v0, u0, v1, u1, v1, u1, v0 };

        float view_w = v_bounds.x1 - v_bounds.x0; float view_h = v_bounds.y1 - v_bounds.y0;
        if (view_h <= 0.1f) view_h = 1.0f;
        
        //int tex_h = 480;
        //int tex_w = (int)(640.0f * (view_w / view_h));

		int tex_h = 480; 
        int tex_w = (int)(tex_h * (view_w / view_h));		

        m_filter.draw_quad(m_fbo_texture, fbo_verts, fbo_uv, tex_w, tex_h, m_view_width, m_view_height);
        
        glUseProgram(m_quad_program);
        m_current_texture = 0; m_last_blendmode = -1;
    };


	for (const local_primitive& prim : draw_prims)
	{
        bool is_screen = PRIMFLAG_GET_SCREENTEX(prim.flags);
		bool is_vector = PRIMFLAG_GET_VECTOR(prim.flags);
        
        if (m_usefilter && is_vector) {
            if (!fbo_active) {
                flush_batch(); 
                glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
                glViewport(0, 0, m_width, m_height);
                glClearColor(0, 0, 0, 0); 
                glClear(GL_COLOR_BUFFER_BIT);
                fbo_active = true;
            }
        } else if (fbo_active && !is_vector) {
            flush_batch(); 
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]); // RESTAURAR VIEWPORT
            fbo_active = false;
            draw_vector_fbo(); 
        }

		GLuint needed_tex = (prim.texture != nullptr) ? prim.texture->texture_id : (is_vector ? m_glow_texture : m_white_texture);
		int needed_blend = PRIMFLAG_GET_BLENDMODE(prim.flags);

		if (m_current_texture != needed_tex || m_last_blendmode != needed_blend) {
			flush_batch();
			m_current_texture = needed_tex; set_blendmode(needed_blend);
			glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_current_texture);
		}

		switch (prim.type)
		{
			case render_primitive::LINE:
			{
				float effwidth = std::max(prim.width, 1.0f);
				bool is_point = ((prim.bounds.x1 - prim.bounds.x0) == 0.0f) && ((prim.bounds.y1 - prim.bounds.y0) == 0.0f);

				if (is_point) {
					float half_w = effwidth * 0.5f;
					m_quad_verts[0] = prim.bounds.x0 - half_w; m_quad_verts[1] = prim.bounds.y0 - half_w; 
					m_quad_verts[2] = prim.bounds.x0 - half_w; m_quad_verts[3] = prim.bounds.y0 + half_w; 
					m_quad_verts[4] = prim.bounds.x0 + half_w; m_quad_verts[5] = prim.bounds.y0 + half_w; 
					m_quad_verts[6] = prim.bounds.x0 + half_w; m_quad_verts[7] = prim.bounds.y0 - half_w; 
					
					float core_uv[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
					render_color c_core = { std::min(prim.color.a * 255.0f, 1.0f), prim.color.r, prim.color.g, prim.color.b };
					push_quad(m_quad_verts, is_vector ? core_uv : nullptr, c_core);
					
					if (is_vector && enable_bloom) {
						float bloom_w = effwidth * 2.0f;
						m_quad_verts[0] = prim.bounds.x0 - bloom_w; m_quad_verts[1] = prim.bounds.y0 - bloom_w; 
						m_quad_verts[2] = prim.bounds.x0 - bloom_w; m_quad_verts[3] = prim.bounds.y0 + bloom_w; 
						m_quad_verts[4] = prim.bounds.x0 + bloom_w; m_quad_verts[5] = prim.bounds.y0 + bloom_w; 
						m_quad_verts[6] = prim.bounds.x0 + bloom_w; m_quad_verts[7] = prim.bounds.y0 - bloom_w; 
						
						float bloom_uv[8] = { 0.5f, 0.0f, 0.5f, 1.0f, 0.5f, 1.0f, 0.5f, 0.0f };
						render_color c_bloom = { std::min(prim.color.a * 255.0f, 1.0f) * 0.25f, prim.color.r, prim.color.g, prim.color.b };
						push_quad(m_quad_verts, bloom_uv, c_bloom);
					}
				} else {
					auto [b0, b1] = render_line_to_quad(prim.bounds, effwidth, 0.0f);
					const line_aa_step* step = PRIMFLAG_GET_ANTIALIAS(prim.flags) ? line_aa_4step : line_aa_1step;
					
					for (; step->weight != 0.0f; step++) {
						render_color c;
						c.a = std::min(prim.color.a * 255.0f, 1.0f); 
						c.r = std::min(prim.color.r * step->weight, 1.0f);
						c.g = std::min(prim.color.g * step->weight, 1.0f);
						c.b = std::min(prim.color.b * step->weight, 1.0f);

						m_quad_verts[0] = b0.x0 + step->xoffs; m_quad_verts[1] = b0.y0 + step->yoffs; 
						m_quad_verts[2] = b0.x1 + step->xoffs; m_quad_verts[3] = b0.y1 + step->yoffs; 
						m_quad_verts[4] = b1.x1 + step->xoffs; m_quad_verts[5] = b1.y1 + step->yoffs; 
						m_quad_verts[6] = b1.x0 + step->xoffs; m_quad_verts[7] = b1.y0 + step->yoffs; 

						float core_uv[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
						push_quad(m_quad_verts, is_vector ? core_uv : nullptr, c);
					}

					if (is_vector && enable_bloom) {
						float bloom_width = effwidth * 4.0f;
						auto [bb0, bb1] = render_line_to_quad(prim.bounds, bloom_width, 0.0f);

						m_quad_verts[0] = bb0.x0; m_quad_verts[1] = bb0.y0; 
						m_quad_verts[2] = bb0.x1; m_quad_verts[3] = bb0.y1; 
						m_quad_verts[4] = bb1.x1; m_quad_verts[5] = bb1.y1; 
						m_quad_verts[6] = bb1.x0; m_quad_verts[7] = bb1.y0; 

						float bloom_uv[8] = { 0.5f, 0.0f, 0.5f, 1.0f, 0.5f, 1.0f, 0.5f, 0.0f };
						render_color c_bloom = { std::min(prim.color.a * 255.0f, 1.0f) * 0.40f, prim.color.r, prim.color.g, prim.color.b };
						push_quad(m_quad_verts, bloom_uv, c_bloom);
					}
				}
			} break;

			case render_primitive::QUAD:
			{
				m_quad_verts[0] = prim.bounds.x0; m_quad_verts[1] = prim.bounds.y0; 
				m_quad_verts[2] = prim.bounds.x0; m_quad_verts[3] = prim.bounds.y1; 
				m_quad_verts[4] = prim.bounds.x1; m_quad_verts[5] = prim.bounds.y1; 
				m_quad_verts[6] = prim.bounds.x1; m_quad_verts[7] = prim.bounds.y0; 

				if (prim.texture) {
					const render_quad_texuv& texuv = prim.texcoords;
					m_quad_uv[0] = texuv.tl.u; m_quad_uv[1] = texuv.tl.v;
					m_quad_uv[2] = texuv.bl.u; m_quad_uv[3] = texuv.bl.v;
					m_quad_uv[4] = texuv.br.u; m_quad_uv[5] = texuv.br.v;
					m_quad_uv[6] = texuv.tr.u; m_quad_uv[7] = texuv.tr.v;

                    if (m_usefilter && is_screen) {
                        flush_batch();
                        set_blendmode(needed_blend);
                        m_filter.draw_quad(m_current_texture, m_quad_verts, m_quad_uv, prim.texture->texinfo.width, prim.texture->texinfo.height, m_view_width, m_view_height);
                        glUseProgram(m_quad_program);
                        m_current_texture = 0; m_last_blendmode = -1;
                    } else {
					    push_quad(m_quad_verts, m_quad_uv, prim.color);
                    }
				} else {
					push_quad(m_quad_verts, nullptr, prim.color);
				}
			} break;

			case render_primitive::INVALID: break;
		}
	}

	flush_batch();
	
	if (m_usefilter && fbo_active) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        draw_vector_fbo();
    }

	if (!delete_texs.empty()) 
		glDeleteTextures(delete_texs.size(), delete_texs.data());
}

static void texture_copy_data(void* dest, const render_texinfo& texinfo, u32 texformat)
{
	for (int y=0; y<texinfo.height; y++)
	{
		uint32_t *dst = (u32*)dest + (texinfo.width * y);
		#define src(T) (T*)texinfo.base + (texinfo.rowpixels * y)

		switch (texformat)
		{
			case TEXFORMAT_RGB32: copy_util::copyline_rgb32(dst, src(u32), texinfo.width, texinfo.palette); break;
			case TEXFORMAT_ARGB32: copy_util::copyline_argb32(dst, src(u32), texinfo.width, texinfo.palette); break;
			case TEXFORMAT_PALETTE16: copy_util::copyline_palette16(dst, src(u16), texinfo.width, texinfo.palette); break;
			case TEXFORMAT_YUY16: copy_util::copyline_yuy16_to_argb(dst, src(u16), texinfo.width, texinfo.palette, 1); break;
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
			if (texture->base_back == nullptr) {
				texture->base_back = std::malloc((texture->texinfo.width * 4) * texture->texinfo.height);
			}
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
	texture->texinfo = texinfo;
	texture->prim_flags = prim.flags;

	texture->base = std::malloc((texinfo.width * 4) * texinfo.height);
	texture->owned = true;

	texture_copy_data(texture->base, texinfo, PRIMFLAG_GET_TEXFORMAT(prim.flags));

    texture->needs_gl_init = true;
	texture->last_access = osd_ticks();

	return texture;
}

std::shared_ptr<gles2_renderer::gles2_texture> gles2_renderer::texture_find(const render_primitive& prim, osd_ticks_t now)
{
	for (auto& tex : m_texlist)
	{
		if (compare_texture_primitive(*tex, prim))
		{
			tex->last_access = now;
			return tex;
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

