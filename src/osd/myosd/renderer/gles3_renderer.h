// license:BSD-3-Clause
// copyright-holders: David Valdeita (Seleuco) & Filipe Paulino (FlykeSpice)
/***************************************************************************

    gles3_renderer.h

    GLES3 renderer for MAME4droid

***************************************************************************/

#pragma once

#ifndef GLES3_RENDERER_H
#define GLES3_RENDERER_H

#include "myosd_renderer.h"

#include "filter_shader.h"

#include "osdcore.h"

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#include <array>
#include <cstdio>
#include <list>
#include <string>
#include <mutex>
#include <memory>
#include <atomic>

typedef uintptr_t HashT;

class gles3_renderer : public myosd_renderer
{
public:
	gles3_renderer(int width, int height, bool use_hdr_display = false, float peak_multiplier = 3.0f);

	void end_renderer() override;

	void sync_state(const render_primitive_list* primlist, bool in_menu = false) override;
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

	struct gles_texture
	{
		HashT hash;
		GLuint texture_id = 0; //GLES texture object id
		render_texinfo texinfo; //Copy of the render_primitive texture info
		u32 prim_flags;  //Copy of the render_primitive flags
		osd_ticks_t last_access;
		
		bool needs_gl_init = false;
		bool needs_gl_update = false;

		void* base = nullptr; //GL_ARGB format
		void* base_back = nullptr;
		bool owned = false; //Do we own the raw data pointer, or is it a direct reference to textinfo.base?

        gles_texture() = default;

		~gles_texture()
		{
			//glDeleteTextures(1, &texture_id);

			if (owned){				
				std::free(base);
				std::free(base_back);
			}
		}

        gles_texture(const gles_texture&) = delete;
        gles_texture& operator=(const gles_texture&) = delete;
	};

	struct local_primitive {
        int type;
		render_bounds bounds;
		render_color color;
		render_quad_texuv texcoords;
        uint32_t flags;
		float width;
		bool needs_texture_upload = false;
		std::shared_ptr<gles_texture> texture;
		void* upload_ptr = nullptr;
		bool is_artwork;		
    };
	
	struct instance_data {
        float p0x, p0y, p1x, p1y; // Vertices 0 and 1
        float p2x, p2y, p3x, p3y; // Vertices 2 and 3
        float u0, v0, u1, v1;     // UVs 0 and 1
        float u2, v2, u3, v3;     // UVs 2 and 3
        float r, g, b, a;         // A single color per instance
    };
	//GL vertex attributes
	static constexpr GLuint ATTRIB_POSITION = 0; 
	static constexpr GLuint ATTRIB_TEXUV = 1; 
	static constexpr GLuint ATTRIB_COLOR = 2;

	static constexpr u8 s_quad_indices[] = { 0, 1, 2, 0, 2, 3 }; //Indices to draw a quad with glDrawElements
	
	~gles3_renderer() override;
	
	inline void set_fbo_dirty() { m_fbo_dirty = true; }
	
private:
	std::mutex m_render_mutex;

    std::vector<local_primitive> m_render_prims; 
    std::vector<GLuint> m_render_textures_to_delete;
    std::vector<GLuint> m_textures_to_delete;
	
	int m_last_blendmode = -1;
	void set_blendmode(int blendmode);

	void update_texture_cache(const render_primitive& prim, std::shared_ptr<gles_texture>& out_tex);
	std::shared_ptr<gles_texture> texture_find(const render_primitive& prim, osd_ticks_t now);
	std::shared_ptr<gles_texture> texture_create(const render_primitive& prim);
	void cleanup_texture_cache();
	
	void upload_pending_textures(std::vector<local_primitive>& draw_prims);
	
	void process_line_primitive(const local_primitive& prim, bool is_vector, bool enable_advanced_effects, float current_time);
	void process_quad_primitive(const local_primitive& prim, bool is_screen, int needed_blend);

	void apply_phosphor_persistence(float fbo_w, float fbo_h);
	void apply_magnetic_jitter(float& px0, float& py0, float& px1, float& py1, bool is_vector, bool enable_advanced_effects, float current_time);	
	void process_dwell_point(const local_primitive& prim, bool is_vector, bool enable_advanced_effects, float current_time, float& prev_x, float& prev_y, float& prev_dx_norm, float& prev_dy_norm);
	
	//Shader program to render a quad primitive
	//each one deals with a specific texture format
	GLuint m_quad_program;
	GLint m_uniform_ortho_quad;
	
	// HDR stuff
	GLuint m_hdr_program;
	GLint m_uniform_ortho_hdr;
	GLint m_uniform_exposure_hdr;
	GLint m_uniform_use_hdr_display;
	GLint m_uniform_base_nits;
    GLint m_uniform_max_nits;	
	GLint m_uniform_peak_nits;
	
	GLint m_loc_quad_use_hdr = -1;
	GLint m_loc_quad_raster_fake_hdr = -1;
	GLint m_loc_quad_raster_hdr_mult = -1;
	GLint m_loc_quad_paper_white = -1;
	GLint m_loc_quad_device_peak = -1;
	
	GLuint m_white_texture = 0;
	GLuint m_glow_texture = 0;
	
	// Auto-Exposure temporal memory
    float m_current_exposure = 1.5f;
	
	// TRUE if the GPU fails to allocate FP16 FBOs and we must force SDR rendering
	bool m_hdr_fallback_active = false;
	
	// --- DUAL FBO SYSTEM ---	
	bool m_fbo_dirty = false;
		
	// Ping-Pong FBOs for HDR Temporal Accumulation
	GLuint m_fbo_hdr[2] = {0, 0};
	GLuint m_fbo_texture_hdr[2] = {0, 0};
	int m_current_hdr_fbo = 0;
	bool m_history_valid = false;
	bool m_multi_monitor_detected = false;
	
	GLuint m_fbo_sdr = 0;
	GLuint m_fbo_texture_sdr = 0;
	
	//Intermediate FBO for applying standard filters in HDR display mode before linearizing
	GLuint m_fbo_filter = 0;
	GLuint m_fbo_texture_filter = 0;
	
	void create_fbos(int width, int height, bool need_hdr, bool need_sdr, bool need_filter);
	void delete_fbos();
	bool calculate_auto_exposure(const std::vector<local_primitive>& draw_prims);	
	void resolve_hdr(GLuint target_fbo, float layout_w, float layout_h, 
		const render_bounds& layout_bounds, const std::array<float, 16>& vector_ortho);
	void switch_fbo_target(int target_fbo, int& current_fbo, bool require_sdr, float layout_w, float layout_h, 
		const render_bounds& layout_bounds, const std::array<float, 16>& vector_ortho, bool has_vectors);
		
	std::vector<instance_data> m_batch_instances;
    
    // Native OpenGL buffers
    GLuint m_corner_vbo = 0;
    GLuint m_instance_vbo = 0;
	GLuint m_vao = 0;

	void flush_batch();
	void push_quad(const float* verts, const float* uv, const render_color& color);

	GLuint m_current_texture = 0;

	std::array<float, 4*4> m_ortho; //Ortho projection matrix

	float m_quad_verts[4*2];
	float m_quad_uv[4*2];

	std::string m_lastfilter; //Last filter used
	bool m_usefilter = false; //If any filter is being used right now
	filter_shader m_filter; //Current filter shader used
    inline static std::vector<std::pair<std::string, filter_data>> s_filters = {};
	
	GLint m_uniform_is_vector_quad;
	int m_last_is_vector = -1;

	int m_width, m_height;

    int m_view_width = 1;
    int m_view_height = 1;
    bool m_init = true;
	std::atomic<bool> m_flush_textures{false};
    int m_last_filter_mode;
	
	bool m_use_hdr_display = false;
	float m_peak_nits;
	
	bool m_in_menu = false;
	
	std::list<std::shared_ptr<gles_texture>> m_texlist; //Currently allocated textures
};

#endif //GLES3_RENDERER_H