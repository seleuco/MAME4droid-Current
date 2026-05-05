// license:BSD-3-Clause
// copyright-holders:Filipe Paulino (FlykeSpice)& David Valdeita (Seleuco)
/***************************************************************************

    gles2_renderer.h

    GLES2 renderer for MAME4droid

***************************************************************************/

#pragma once

#ifndef GLES2_RENDERER_H
#define GLES2_RENDERER_H

#include "myosd_renderer.h"

#include "filter_shader.h"

#include "osdcore.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <array>
#include <cstdio>
#include <list>
#include <string>
#include <mutex>
#include <memory>

typedef uintptr_t HashT;

class gles2_renderer : public myosd_renderer
{
public:
	gles2_renderer(int width, int height);

	void end_renderer() override;

	void sync_state(const render_primitive_list* primlist) override;
	void render() override;

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
		GLuint texture_id = 0; //GLES2 texture object id
		render_texinfo texinfo; //Copy of the render_primitive texture info
		u32 prim_flags;  //Copy of the render_primitive flags
		osd_ticks_t last_access;
		
		bool has_border = false;
		int upload_width = 0;
		int upload_height = 0;

		bool needs_gl_init = false;
        bool needs_gl_update = false;

		void* base = nullptr; //GL_ARGB format
		void* base_back = nullptr;
		bool owned = false; //Do we own the raw data pointer, or is it a direct reference to textinfo.base?

        gles2_texture() = default;

		~gles2_texture()
		{
			//glDeleteTextures(1, &texture_id);

			if (owned){				
				std::free(base);
				std::free(base_back);
			}
		}

        gles2_texture(const gles2_texture&) = delete;
        gles2_texture& operator=(const gles2_texture&) = delete;
	};

	struct local_primitive {
        int type;
        render_bounds bounds;
        render_color color;
        render_quad_texuv texcoords;
        uint32_t flags;
		float width;
		bool needs_texture_upload = false;
        std::shared_ptr<gles2_texture> texture;
		void* upload_ptr = nullptr;
    };

	//GL vertex attributes
	static constexpr GLuint ATTRIB_POSITION = 0;
	static constexpr GLuint ATTRIB_TEXUV    = 1;

	static constexpr u8 s_quad_indices[] = { 0, 1, 2, 0, 2, 3 }; //Indices to draw a quad with glDrawElements

    ~gles2_renderer() override
    {
        glDeleteProgram(m_quad_program);
        glDeleteProgram(m_line_program);

        if (!m_textures_to_delete.empty()) {
            glDeleteTextures(m_textures_to_delete.size(), m_textures_to_delete.data());
            m_textures_to_delete.clear();
        }
		
		if (!m_render_textures_to_delete.empty()) {
            glDeleteTextures(m_render_textures_to_delete.size(), m_render_textures_to_delete.data());
            m_render_textures_to_delete.clear();
        }		

        for (auto& tex : m_texlist) {
            if (tex->texture_id > 0) {
                glDeleteTextures(1, &tex->texture_id);
            }
        }
        m_texlist.clear();
    }

private:
	std::mutex m_render_mutex;

    std::vector<local_primitive> m_render_prims; 
    std::vector<GLuint> m_render_textures_to_delete;
    std::vector<GLuint> m_textures_to_delete;
	
	GLuint m_last_program = 0;
	void use_quad_program();
	void use_line_program();

	int m_last_blendmode = -1;
	void set_blendmode(int blendmode);

	void update_texture_cache(const render_primitive& prim, std::shared_ptr<gles2_texture>& out_tex);
	std::shared_ptr<gles2_texture> texture_find(const render_primitive& prim, osd_ticks_t now);
	std::shared_ptr<gles2_texture> texture_create(const render_primitive& prim);

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
    inline static std::vector<std::pair<std::string, filter_data>> s_filters = {};

	int m_width, m_height;

    int m_view_width = 1;
    int m_view_height = 1;
    bool m_force_viewport_update = true;
    bool m_flush_textures = false;
    int m_last_filter_mode;
	
	std::list<std::shared_ptr<gles2_texture>> m_texlist; //Currently allocated textures
};

#endif //GLES2_RENDERER_H
