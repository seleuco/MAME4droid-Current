// license:BSD-3-Clause
// copyright-holders: David Valdeita (Seleuco) & Filipe Paulino (FlykeSpice)
/***************************************************************************

    gles3_renderer.cpp

    GL MAME Native renderer based on GLES 3.x for MAME4droid

***************************************************************************/

#include "gles3_renderer.h"
#include "gl_utils.hxx"

#include "shader_sources.hxx"

#include "modules/render/copyutil.h"

#include <android/log.h>

#include <string>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <utility>

#define ANDROID_LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "gles3_renderer", __VA_ARGS__)



// =======================================================================
// VECTOR BLOOM EFFECT CONFIGURATION
// =======================================================================

// Forces the use of the FBO for vectors whenever bloom is enabled,
// even if no MAME CRT filter (shader) is selected.
constexpr bool BLOOM_FORCE_FBO = true;


constexpr int VECTOR_FBO_HEIGHT =  480; 

// =======================================================================
// HDR TONE MAPPING (EXPOSURE)
// =======================================================================
// - Purpose: Prevents the 8-bit FBO from clamping bright vectors. Dims the raw RGB 
//   colors so additive bloom has "headroom" to build up without hitting the 1.0 ceiling.
// - Suggested Range: [0.30f - 0.70f]. (0.45f leaves enough headroom for bloom accumulation).
constexpr float BLOOM_EXPOSURE_RGB = 0.45f;

// =======================================================================
// VECTOR BLOOM EFFECT CONFIGURATION
// =======================================================================

// Defines the light falloff curve (Gaussian) for the CRT Phosphor.
// - Purpose: Controls how sharp or soft the core of the line is compared to its glowing halo.
// - Suggested Range: [3.0f - 6.0f] 
//   -> 3.0f = Modern HDR bloom (very soft, nebulous).
//   -> 5.0f = Sweet spot (solid core, smooth natural decay).
//   -> 6.0f = Pure 80s Arcade (extremely tight core, abrupt halo).
//constexpr float BLOOM_PHOSPHOR_FALLOFF = 5.0f;
constexpr float BLOOM_PHOSPHOR_FALLOFF = 4.5f;

// --- Lines (Standard vectors) ---
// - Purpose: Base physical width (in pixels) and base opacity for drawing standard lines.
// - Width Range: [3.0f - 6.0f] (4.0f is generally safe).
// - Alpha Range: [0.50f - 1.0f] (0.75f allows some transparency before HDR kicks in).
//constexpr float BLOOM_LINE_WIDTH_MULT = 3.5f;  
//constexpr float BLOOM_LINE_ALPHA      = 0.75f;

constexpr float BLOOM_LINE_WIDTH_MULT = 3.5f;  
constexpr float BLOOM_LINE_ALPHA      = 0.75f;

// --- Points (Stars / Shots / Explosions) ---
// - Purpose: Base physical width and opacity for drawing single points (vertices).
// - Width Range: [2.0f - 4.0f] (Keep it smaller than lines so stars look sharp).
// - Alpha Range: [0.40f - 0.85f] (Points naturally overlap less, 0.55f is a good baseline).
//constexpr float BLOOM_POINT_WIDTH_MULT = 2.5f; 
//constexpr float BLOOM_POINT_ALPHA      = 0.55f;
constexpr float BLOOM_POINT_WIDTH_MULT = 4.5f; 
constexpr float BLOOM_POINT_ALPHA      = 0.85f;

// =======================================================================
// DUAL-LOBE PHOSPHOR CONFIGURATION (CRT OPTICS)
// =======================================================================

// 1. Core Lobe Sharpness (Laser impact)
// - Purpose: Defines how sharp and bright the pure core of the vector is.
// - Suggested Range: [8.0f - 16.0f]. Higher value = thinner and harder laser. (12.0f recommended)
//constexpr float BLOOM_CORE_SHARPNESS = 12.0f;
constexpr float BLOOM_CORE_SHARPNESS = 14.0f;

// 2. Secondary Lobe Spread (Scattering halo)
// - Purpose: How far the light travels inside the CRT tube glass.
// - Suggested Range: [1.5f - 4.0f]. Lower value = wider halo. (2.5f recommended)
constexpr float BLOOM_GLOW_SPREAD = 2.5f;
//constexpr float BLOOM_GLOW_SPREAD = 1.2f;

// 3. Secondary Lobe Weight (Halo opacity)
// - Purpose: The intensity of the light fog surrounding the laser.
// - Suggested Range: [0.15f - 0.50f]. Higher value = thicker light fog. (0.35f recommended)
constexpr float BLOOM_GLOW_WEIGHT = 0.35f;
//constexpr float BLOOM_GLOW_WEIGHT = 0.65f;


// -----------------------------------------------------------------------
// EXCESS LIGHT PHYSICS (OVERBRIGHT / HDR)
// -----------------------------------------------------------------------

// The absolute maximum limit for extra HDR energy a vector can accumulate.
// - Purpose: Acts as a safety ceiling to prevent the bloom from completely white-washing the screen.
// - Suggested Range: [1.5f - 3.0f] (2.5f allows bright flashes without blinding the player).
//constexpr float BLOOM_OVERBRIGHT_MAX = 2.5f;
constexpr float BLOOM_OVERBRIGHT_MAX = 2.5f;

// How much lines and points physically expand their radius when overloaded with energy.
// - Purpose: Simulates the phosphor bleeding light into adjacent areas when saturated.
// - Suggested Range: [0.30f - 0.70f] (Above 0.8f, the lines will look like fat neon tubes).
constexpr float BLOOM_OVERBRIGHT_LINE_MULT  = 0.55f; 
//constexpr float BLOOM_OVERBRIGHT_POINT_MULT = 0.45f;
//constexpr float BLOOM_OVERBRIGHT_LINE_MULT  = 0.65f; 
constexpr float BLOOM_OVERBRIGHT_POINT_MULT = 1.35f;


// -----------------------------------------------------------------------
// CRT GLOBAL DRIVE (MONITOR VOLTAGE / BRIGHTNESS)
// -----------------------------------------------------------------------

// Global energy multiplier applied to the raw alpha value provided by MAME.
// - Purpose: Simulates turning the "Brightness" or "Drive" knob on the back of the arcade monitor.
// - Suggested Range: [1.0f - 2.0f]
//   -> 1.0f = Dark, accurate, strictly follows MAME's alpha.
//   -> 1.35f = Recommended (Arcade monitor running slightly overdriven).
//   -> 1.8f+ = Extremely bright, almost everything will generate bloom.
//constexpr float BLOOM_GLOBAL_DRIVE_MULTIPLIER = 1.35f;
constexpr float BLOOM_GLOBAL_DRIVE_MULTIPLIER = 1.35f;


// -----------------------------------------------------------------------
// BEAM SPEED PHYSICS (INTENSITY DYNAMICS)
// -----------------------------------------------------------------------

// What percentage of the screen height is considered a "short" line.
// - Purpose: Identifies high-energy strokes like text characters, ships, or small details.
// - Suggested Range: [0.02f - 0.10f] (0.04f = 4% of screen height, perfect for standard text).
constexpr float BLOOM_SHORT_LINE_THRESHOLD_PCT = 0.04f;

// How much extra light energy a tiny line receives.
// - Purpose: Simulates the electron beam burning the phosphor harder because it's moving less distance.
// - Suggested Range: [0.50f - 2.0f] (1.0f = +100% extra energy, makes text highly legible).
//constexpr float BLOOM_SHORT_LINE_INTENSITY_BOOST = 1.0f;
constexpr float BLOOM_SHORT_LINE_INTENSITY_BOOST = 0.5f;

// How much the core physically widens when burning the phosphor harder.
// - Purpose: Simulates thermal expansion of the dot on the screen.
// - Suggested Range: [0.10f - 0.40f] (0.20f = +20% thicker core for short lines).
//constexpr float BLOOM_SHORT_LINE_WIDTH_BOOST = 0.20f;
constexpr float BLOOM_SHORT_LINE_WIDTH_BOOST = 0.10f;

// =======================================================================
// BEAM INERTIA & DWELL TIME (CORNER BURN)
// =======================================================================

// Master toggle to enable or disable the corner burn effect entirely.
constexpr bool BLOOM_CORNER_BURN_ENABLED = true;

// 1. Angular Threshold (Dot Product)
// - Purpose: How sharp a turn must be to cause the beam to decelerate and burn the corner.
// - Suggested Range: [0.30f - 0.70f]. 
//   -> 0.50f (60 degrees) triggers burns on sharp polygons like the Asteroids ship.
constexpr float BLOOM_CORNER_DOT_THRESHOLD = 0.50f;

// 2. Corner Burn Intensity Boost
// - Purpose: How much extra energy is dumped into the phosphor during the dwell time.
// - Suggested Range: [1.0f - 3.0f]. (1.5f provides a beautiful glowing weld effect at vertices).
//constexpr float BLOOM_CORNER_BURN_BOOST = 1.5f;
constexpr float BLOOM_CORNER_BURN_BOOST = 1.7;

// 3. Corner Burn Physical Size
// - Purpose: Confines the extra light inside the vector path to prevent spherical blobs at vertices.
// - Suggested Range: [0.15f - 0.30f]. (0.20f keeps the burn intense but visually sharp).
//constexpr float BLOOM_CORNER_BURN_WIDTH_MULT = 0.20f;
constexpr float BLOOM_CORNER_BURN_WIDTH_MULT = 0.30f;

// -----------------------------------------------------------------------
// ANALOG IMPERFECTIONS (NOISE & MAGNETIC JITTER)
// -----------------------------------------------------------------------

// Maximum physical deviation of the beam due to magnetic coil noise/heat (in pixels).
// - Purpose: Adds a subtle, living vibration to the vectors, breaking the "perfect digital" look.
// - Suggested Range: [0.0f - 0.60f] 
//   -> 0.0f = Off (Perfectly stable lines).
//   -> 0.35f = Recommended (Subtle electric hum).
//   -> 0.60f+ = Heavy wear/damaged yoke (Looks like a broken monitor).
//constexpr float BLOOM_BEAM_JITTER_AMOUNT = 0.35f; 
constexpr float BLOOM_BEAM_JITTER_AMOUNT = 0.15f;

// =======================================================================
// PHOSPHOR COLOR RESPONSE (LUMINANCE & BLEED)
// =======================================================================

// Master toggle to enable or disable the phosphor color response (luminance calculation).
// - Purpose: Disabling this treats all colors (Red, Green, Blue) equally with 100% efficiency.
//   Turn to 'false' if you feel certain colors (like pure Blue) are too dim.
constexpr bool BLOOM_PHOSPHOR_RESPONSE_ENABLED = false;

// 1. Perceptual Color Weights (Rec.601 / NTSC standard)
// - Purpose: Defines how strongly each color excites the CRT phosphor.
//   Green is highly efficient and bleeds heavily. Blue is inefficient and tight.
constexpr float BLOOM_PHOSPHOR_WEIGHT_R = 0.299f;
constexpr float BLOOM_PHOSPHOR_WEIGHT_G = 0.587f;
constexpr float BLOOM_PHOSPHOR_WEIGHT_B = 0.114f;

// 2. Base Phosphor Response (Floor)
// - Purpose: The minimum energy retained by the darkest/least efficient color (Blue).
// - Suggested Range: [0.30f - 0.50f]. (0.40f ensures blue vectors remain visible).
constexpr float BLOOM_PHOSPHOR_BASE_RESPONSE = 0.40f;

// 3. Luminance Multiplier
// - Purpose: How much the calculated color luminance boosts the final beam energy.
// - Suggested Range: [0.40f - 0.80f]. (0.60f combined with a 0.40f base perfectly caps at 1.0).
constexpr float BLOOM_PHOSPHOR_LUMA_BOOST = 0.60f;


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

using gles_texture = gles3_renderer::gles_texture;

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
static bool compare_texture_primitive(const gles_texture& texture, const render_primitive& prim);

void gles3_renderer::set_shader(const char* shader_name)
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

std::vector<std::string> gles3_renderer::get_shaders_supported()
{
    std::vector<std::string> key_list;
    for (const auto& [key, value] : s_filters)
        key_list.push_back(key);

    return key_list;
}

gles3_renderer::gles3_renderer(int width, int height)
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

	m_quad_program = gl_utils::create_program(quad_vertex_shader, quad_frag_shader, {
        {0, "a_corner"}, {1, "i_p0p1"}, {2, "i_p2p3"}, 
        {3, "i_uv0uv1"}, {4, "i_uv2uv3"}, {5, "i_color"}
    });
	
	GLuint hdr_frag_shader = gl_utils::load_shader(hdr_frag_shader_src, GL_FRAGMENT_SHADER);
	m_hdr_program = gl_utils::create_program(quad_vertex_shader, hdr_frag_shader, {
        {0, "a_corner"}, {1, "i_p0p1"}, {2, "i_p2p3"}, 
        {3, "i_uv0uv1"}, {4, "i_uv2uv3"}, {5, "i_color"}
    });
	glDeleteShader(hdr_frag_shader);
	m_uniform_ortho_hdr = glGetUniformLocation(m_hdr_program, "u_ortho");
	m_uniform_exposure_hdr = glGetUniformLocation(m_hdr_program, "u_exposure");
	glUseProgram(m_hdr_program);
	glUniform1i(glGetUniformLocation(m_hdr_program, "s_texture"), 0);
	glUseProgram(0);	
	
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

	glGenTextures(1, &m_glow_texture); // GLOW texture
	glBindTexture(GL_TEXTURE_2D, m_glow_texture);
	uint32_t glow_pixels[64 * 64];
	for (int y = 0; y < 64; y++) {
		for (int x = 0; x < 64; x++) {
			// Instead of treating x and y equally, we apply "Astigmatism" to the glass.
			// By compressing X, the light reaches further (wider halo horizontally).
			// By expanding Y, the light cuts off sooner (narrower halo vertically).
			float dx = ((x - 31.5f) / 31.5f) * 0.85f; // Travels easier horizontally
			float dy = ((y - 31.5f) / 31.5f) * 1.15f; // Has more resistance vertically
			float dist = std::sqrt(dx*dx + dy*dy);
			
			// --- DUAL-LOBE PHOSPHOR OPTICS ---
			// 1. Dense and sharp core (Beam impact)
			float core = std::exp(-(dist * dist) * BLOOM_CORE_SHARPNESS); 
			
			// 2. Soft and expansive halo (Optical scattering)
			float glow = std::exp(-(dist * dist) * BLOOM_GLOW_SPREAD) * BLOOM_GLOW_WEIGHT; 
			
			// Add both lobes and clamp to 1.0 for mathematical safety
			float intensity = std::min(core + glow, 1.0f);
			// ---------------------------------
			
			uint8_t a = (uint8_t)(intensity * 255.0f);
			glow_pixels[y * 64 + x] = (a << 24) | 0x00FFFFFF; 
		}
	}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, glow_pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	
	// Static base geometry (6 indices to form 2 triangles)
    glGenBuffers(1, &m_corner_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_corner_vbo);
    float corners[6] = {0.0f, 1.0f, 2.0f, 0.0f, 2.0f, 3.0f};
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);

    // Dynamic buffer for instance data
    glGenBuffers(1, &m_instance_vbo);

    m_batch_instances.reserve(4096);
	
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	on_emulatedsize_change(width, height);
}

void gles3_renderer::end_renderer()
{
    m_flush_textures = true;
}

void gles3_renderer::on_emulatedsize_change(int width, int height)
{
    std::lock_guard<std::mutex> lock(m_render_mutex);	
	
	m_ortho = gl_utils::make_ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);
	m_width = width; m_height = height;

    m_last_filter_mode = myosd_get(MYOSD_BITMAP_FILTERING);

    m_flush_textures = true;
    m_filter.set_ortho(m_ortho);
	
	m_fbo_dirty = true;
	
	m_init = true;
}

void gles3_renderer::create_fbo(int width, int height, bool is_hdr) {
	delete_fbo();
    glGenTextures(1, &m_fbo_texture);
    glBindTexture(GL_TEXTURE_2D, m_fbo_texture);
	
    if (is_hdr) {
        // --- TRUE HDR 16-BIT FLOAT FORMAT ---
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    } else {
        // --- STANDARD 8-BIT FORMAT (Shaders ) ---
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
	
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fbo_texture, 0);
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) 
		ANDROID_LOG("Error creando FBO: %d", status);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}




void gles3_renderer::delete_fbo() {
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    if (m_fbo_texture) glDeleteTextures(1, &m_fbo_texture);
    m_fbo = 0; m_fbo_texture = 0;
}


void gles3_renderer::set_blendmode(int blendmode)
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

void gles3_renderer::sync_state(const render_primitive_list* primlist)
{
	//clean old textures
	cleanup_texture_cache();
	
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

void gles3_renderer::push_quad(const float* verts, const float* uv, const render_color& color) 
{
    // Flush if we reach a safe batch size (around 10k quads at once)
    if (m_batch_instances.size() >= 10000) { 
        flush_batch();
    }
    
    static const float default_uv[8] = {0.0f};
    const float* actual_uv = uv ? uv : default_uv;
    
    // Pack everything into a single contiguous data block
    m_batch_instances.push_back({
        verts[0], verts[1], verts[2], verts[3],
        verts[4], verts[5], verts[6], verts[7],
        actual_uv[0], actual_uv[1], actual_uv[2], actual_uv[3],
        actual_uv[4], actual_uv[5], actual_uv[6], actual_uv[7],
        color.r, color.g, color.b, color.a
    });
}

void gles3_renderer::flush_batch() 
{
    if (m_batch_instances.empty()) return;
	
    // Explicitly protect external VAOs before binding our buffers
    glBindVertexArray(0);	

    // 1. Send the master block of instance data to the GPU
    glBindBuffer(GL_ARRAY_BUFFER, m_instance_vbo);
    glBufferData(GL_ARRAY_BUFFER, m_batch_instances.size() * sizeof(instance_data), m_batch_instances.data(), GL_DYNAMIC_DRAW);

    // 2. Configure the corner buffer (Advances once per VERTEX)
    glBindBuffer(GL_ARRAY_BUFFER, m_corner_vbo);
    glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribDivisor(0, 0); 

    // 3. Configure the instance buffer (Advances once per INSTANCE)
    glBindBuffer(GL_ARRAY_BUFFER, m_instance_vbo);
    int stride = sizeof(instance_data);
    
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(12 * sizeof(float)));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)(16 * sizeof(float)));

    for (int i = 1; i <= 5; i++) {
        glEnableVertexAttribArray(i);
        glVertexAttribDivisor(i, 1); // THIS is the magic Instancing instruction
    }

    // 4. DRAW EVERYTHING AT ONCE! (6 corners per instance)
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, m_batch_instances.size());

    // 5. CLEANUP: Absolute State Isolation
    // Explicitly close all pipelines (Attributes 0 to 5)
    for (int i = 0; i <= 5; i++) {
        glDisableVertexAttribArray(i);
    }
    // Reset the instancing divisor
    for (int i = 1; i <= 5; i++) {
        glVertexAttribDivisor(i, 0); 
    }
    // Unbind the buffer so MAME doesn't read it by mistake
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_batch_instances.clear();
}

void gles3_renderer::upload_pending_textures(std::vector<local_primitive>& draw_prims)
{
	for (local_primitive& prim : draw_prims) {
		if (prim.texture && (prim.texture->needs_gl_init || prim.needs_texture_upload)) {
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
			} else if (prim.needs_texture_upload) {
				glBindTexture(GL_TEXTURE_2D, prim.texture->texture_id);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, prim.texture->texinfo.width, prim.texture->texinfo.height, GL_RGBA, GL_UNSIGNED_BYTE, prim.upload_ptr);
			}
			prim.needs_texture_upload = false; 
		}
	}
}

void gles3_renderer::calculate_vector_bounds(const std::vector<local_primitive>& draw_prims, render_bounds& out_bounds)
{
    out_bounds = { 99999.0f, 99999.0f, -99999.0f, -99999.0f };
    bool has_vectors = false;
    
    for (const local_primitive& prim : draw_prims) {
        if (PRIMFLAG_GET_VECTOR(prim.flags)) {
            float min_x = std::min(prim.bounds.x0, prim.bounds.x1); float max_x = std::max(prim.bounds.x0, prim.bounds.x1);
            float min_y = std::min(prim.bounds.y0, prim.bounds.y1); float max_y = std::max(prim.bounds.y0, prim.bounds.y1);
            out_bounds.x0 = std::min(out_bounds.x0, min_x); out_bounds.y0 = std::min(out_bounds.y0, min_y);
            out_bounds.x1 = std::max(out_bounds.x1, max_x); out_bounds.y1 = std::max(out_bounds.y1, max_y);
            has_vectors = true;
        }
    }

    if (has_vectors) {
        out_bounds.x0 = std::max(0.0f, out_bounds.x0 - 15.0f); out_bounds.y0 = std::max(0.0f, out_bounds.y0 - 15.0f);
        out_bounds.x1 = std::min((float)m_width, out_bounds.x1 + 15.0f); out_bounds.y1 = std::min((float)m_height, out_bounds.y1 + 15.0f);
    }
    
}

/*
void gles3_renderer::draw_vector_fbo(const render_bounds& v_bounds)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    float u0 = v_bounds.x0 / m_width; float u1 = v_bounds.x1 / m_width;
    float v0 = 1.0f - (v_bounds.y0 / m_height); float v1 = 1.0f - (v_bounds.y1 / m_height);

    float fbo_verts[8] = { v_bounds.x0, v_bounds.y0, v_bounds.x0, v_bounds.y1, v_bounds.x1, v_bounds.y1, v_bounds.x1, v_bounds.y0 };
    float fbo_uv[8] = { u0, v0, u0, v1, u1, v1, u1, v0 };

    float view_w = v_bounds.x1 - v_bounds.x0; float view_h = v_bounds.y1 - v_bounds.y0;
    if (view_h <= 0.1f) view_h = 1.0f;
    
    int tex_h = VECTOR_FBO_HEIGHT; 
    int tex_w = (int)(tex_h * (view_w / view_h));		

    m_filter.draw_quad(m_fbo_texture, fbo_verts, fbo_uv, tex_w, tex_h, m_view_width, m_view_height);
    
    glUseProgram(m_quad_program);
    m_current_texture = 0; m_last_blendmode = -1;
}
*/

void gles3_renderer::draw_vector_fbo(const render_bounds& v_bounds)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    float u0 = v_bounds.x0 / m_width; float u1 = v_bounds.x1 / m_width;
    float v0 = 1.0f - (v_bounds.y0 / m_height); float v1 = 1.0f - (v_bounds.y1 / m_height);

    float fbo_verts[8] = { v_bounds.x0, v_bounds.y0, v_bounds.x0, v_bounds.y1, v_bounds.x1, v_bounds.y1, v_bounds.x1, v_bounds.y0 };
    float fbo_uv[8] = { u0, v0, u0, v1, u1, v1, u1, v0 };

    float view_w = v_bounds.x1 - v_bounds.x0; float view_h = v_bounds.y1 - v_bounds.y0;
    if (view_h <= 0.1f) view_h = 1.0f;
    
    int tex_h = 480; 
    int tex_w = (int)(tex_h * (view_w / view_h));		

	if (m_usefilter) {
        // With Shader (8-Bit FBO) ---
        m_filter.draw_quad(m_fbo_texture, fbo_verts, fbo_uv, tex_w, tex_h, m_view_width, m_view_height);
    } else {
        //Pure FBO to Screen ---
        GLuint program_to_use = m_fbo_is_hdr ? m_hdr_program : m_quad_program;
        glUseProgram(program_to_use);
        
        if (m_fbo_is_hdr) {
            glUniformMatrix4fv(m_uniform_ortho_hdr, 1, GL_FALSE, m_ortho.data());
            // Pass our exposure to the final Tone Mapper			
            // Strong photographic exposure so the light overexposes naturally.
            //TODO: Revisar constante hardcodeada
            glUniform1f(m_uniform_exposure_hdr, 1.5f);
        } else {
            glUniformMatrix4fv(m_uniform_ortho_quad, 1, GL_FALSE, m_ortho.data());
        }
        
        m_current_texture = m_fbo_texture;
        glBindTexture(GL_TEXTURE_2D, m_current_texture);

        render_color color = { 1.0f, 1.0f, 1.0f, 1.0f }; 
        push_quad(fbo_verts, fbo_uv, color);
        flush_batch();
    }
    
    glUseProgram(m_quad_program);
    m_current_texture = 0; m_last_blendmode = -1;
}

void gles3_renderer::process_dwell_point(const local_primitive& prim, bool is_vector, bool enable_bloom, float current_time, float& prev_x, float& prev_y, float& prev_dx_norm, float& prev_dy_norm)
{
    // Early exit if the effect is disabled globally or not applicable
    if (!BLOOM_CORNER_BURN_ENABLED || !is_vector || !enable_bloom) {
        return; 
    }

    float px0 = prim.bounds.x0; float py0 = prim.bounds.y0;
    float px1 = prim.bounds.x1; float py1 = prim.bounds.y1;
    
    float dx = px1 - px0;
    float dy = py1 - py0;
    float len = std::sqrt(dx*dx + dy*dy);
    
    if (len > 0.001f) {
        float dx_norm = dx / len;
        float dy_norm = dy / len;
        
        // If the current line starts where the previous one ended (continuous stroke)
        if (std::abs(px0 - prev_x) < 1.0f && std::abs(py0 - prev_y) < 1.0f) {
            
            // Dot Product to find the turn angle
            float dot = (prev_dx_norm * dx_norm) + (prev_dy_norm * dy_norm);
            
            // If the turn is sharper than our threshold
            if (dot < BLOOM_CORNER_DOT_THRESHOLD) {
                
                // Calculate brake aggressiveness (0.0 to 1.0)
                float sharpness = (BLOOM_CORNER_DOT_THRESHOLD - dot) / (BLOOM_CORNER_DOT_THRESHOLD + 1.0f);
                
                // INJECT A "DWELL POINT" (Corner Burn)
                local_primitive corner_prim = prim;
                corner_prim.bounds.x0 = px0; corner_prim.bounds.x1 = px0;
                corner_prim.bounds.y0 = py0; corner_prim.bounds.y1 = py0;
                
                // Confine the light physically inside the vector
                corner_prim.width = prim.width * BLOOM_CORNER_BURN_WIDTH_MULT;
                
                // Boost the point's energy based on turn sharpness
                float energy_boost = 1.0f + (sharpness * BLOOM_CORNER_BURN_BOOST);
                corner_prim.color.a = std::min(prim.color.a * energy_boost, 1.0f);
                
                // Draw the bright point before drawing the actual line
                process_line_primitive(corner_prim, is_vector, enable_bloom, current_time);
            }
        }
        
        // Save the beam state to compare with the next line
        prev_x = px1;
        prev_y = py1;
        prev_dx_norm = dx_norm;
        prev_dy_norm = dy_norm;
        
    } else {
        // If length is 0 (it was an isolated point), break the continuous stroke
        prev_x = -9999.0f; 
    }
}

void gles3_renderer::process_line_primitive(const local_primitive& prim, bool is_vector, bool enable_bloom, float current_time)
{
    float effwidth = std::max(prim.width, 1.0f);

    // --- COHERENT MAGNETIC WOBBLE (Analog Jitter) ---
    float px0 = prim.bounds.x0; float py0 = prim.bounds.y0;
    float px1 = prim.bounds.x1; float py1 = prim.bounds.y1;
	
    if (is_vector && enable_bloom) {
        float center_x = (px0 + px1) * 0.5f; float center_y = (py0 + py1) * 0.5f;
        float jx = std::sin(current_time * 13.2f + center_y * 0.05f) * BLOOM_BEAM_JITTER_AMOUNT;
        float jy = std::cos(current_time * 11.7f + center_x * 0.05f) * BLOOM_BEAM_JITTER_AMOUNT;
        px0 += jx; py0 += jy; px1 += jx; py1 += jy;
    }
    
    float dx = px1 - px0; float dy = py1 - py0;
    bool is_point = (std::abs(dx) < 0.001f && std::abs(dy) < 0.001f);
    float length = is_point ? 0.0f : std::sqrt(dx*dx + dy*dy);
    
	// 1. Get raw MAME intensity (0.0 to 1.0)
    float base_intensity = prim.color.a; 

    // --- PHOSPHOR COLOR RESPONSE (Luminance & Bleed) ---
    float phosphor_response = 1.0f;
	float drive = enable_bloom ? BLOOM_GLOBAL_DRIVE_MULTIPLIER : 1.0f; 
    
    if (enable_bloom && BLOOM_PHOSPHOR_RESPONSE_ENABLED) {
        float luminance = (prim.color.r * BLOOM_PHOSPHOR_WEIGHT_R) + (prim.color.g * BLOOM_PHOSPHOR_WEIGHT_G) + (prim.color.b * BLOOM_PHOSPHOR_WEIGHT_B);
        phosphor_response = BLOOM_PHOSPHOR_BASE_RESPONSE + (luminance * BLOOM_PHOSPHOR_LUMA_BOOST);
    }

    // 2. Apply simulated energy physics
    float simulated_energy = base_intensity * drive * phosphor_response;

    // 3. BEAM SPEED DYNAMICS (Electron gun physics)
    if (is_vector && !is_point) {
        float threshold = m_height * BLOOM_SHORT_LINE_THRESHOLD_PCT; 
        if (length < threshold && length > 0.1f) {
            float shortness = 1.0f - (length / threshold);
            simulated_energy *= (1.0f + shortness * BLOOM_SHORT_LINE_INTENSITY_BOOST);
            effwidth *= (1.0f + shortness * BLOOM_SHORT_LINE_WIDTH_BOOST);
        }
    }

    // ====================================================================
    // 4 & 5. ROUTE ENERGY BASED ON FBO CAPABILITIES
    // ====================================================================
    float col_r, col_g, col_b;
    float core_alpha, safe_bloom_alpha_point, safe_bloom_alpha_line;
    float overbright = 0.0f;

    if (m_fbo_is_hdr) {
        // --- TRUE HDR PATH (16-Bit Float) ---
        // FBO has no limits. Physical energy goes directly into RGB channels!
        col_r = prim.color.r * simulated_energy;
        col_g = prim.color.g * simulated_energy;
        col_b = prim.color.b * simulated_energy;
        
        // Alpha acts strictly as a spatial mask for blending, not as a light cap.
        core_alpha = 1.0f; 
        safe_bloom_alpha_point = BLOOM_POINT_ALPHA;
        safe_bloom_alpha_line  = BLOOM_LINE_ALPHA;
        
        // Calculate overbright for physical expansion (bloom radius)
        float overbright_raw = std::max(simulated_energy - 1.0f, 0.0f);
        overbright = std::pow(overbright_raw, 0.7f); // No strict cap needed
        
    } else {
        // --- 8-BIT PATH (MAME Shaders) ---
        // FBO clamps at 1.0. We use exposure and whitewashing to fake HDR.
        float exposure = (is_vector && enable_bloom) ? BLOOM_EXPOSURE_RGB : 1.0f;
        col_r = prim.color.r * exposure;
        col_g = prim.color.g * exposure;
        col_b = prim.color.b * exposure;

        core_alpha = enable_bloom ? (1.0f - std::exp(-simulated_energy * 1.5f)) : std::min(simulated_energy, 1.0f);
        
        float overbright_raw = std::max(simulated_energy - 1.0f, 0.0f);
        overbright = std::min(std::pow(overbright_raw, 0.7f), BLOOM_OVERBRIGHT_MAX);

        if (enable_bloom && overbright > 0.0f) {
            float desaturation = std::min(overbright * 0.3f, 1.0f);
            col_r = col_r + (1.0f - col_r) * desaturation;
            col_g = col_g + (1.0f - col_g) * desaturation;
            col_b = col_b + (1.0f - col_b) * desaturation;
        }

        float max_alpha = 0.85f + (overbright * 0.1f);
        safe_bloom_alpha_point = std::min(simulated_energy * BLOOM_POINT_ALPHA, max_alpha);
        safe_bloom_alpha_line  = std::min(simulated_energy * BLOOM_LINE_ALPHA, max_alpha);
        
        if (simulated_energy > 0.01f) {
            safe_bloom_alpha_point = std::max(0.08f, safe_bloom_alpha_point);
            safe_bloom_alpha_line  = std::max(0.08f, safe_bloom_alpha_line);
        }
    }
    
    float bloom_scale = 1.0f;

    // ====================================================================
    // 6. DRAW PRIMITIVES
    // ====================================================================
    if (is_point) {
        if (is_vector && enable_bloom) {
            float dynamic_width = BLOOM_POINT_WIDTH_MULT + (overbright * BLOOM_OVERBRIGHT_POINT_MULT);
            float bloom_w = effwidth * dynamic_width * bloom_scale;
            
            m_quad_verts[0] = px0 - bloom_w; m_quad_verts[1] = py0 - bloom_w; 
            m_quad_verts[2] = px0 - bloom_w; m_quad_verts[3] = py0 + bloom_w; 
            m_quad_verts[4] = px0 + bloom_w; m_quad_verts[5] = py0 + bloom_w; 
            m_quad_verts[6] = px0 + bloom_w; m_quad_verts[7] = py0 - bloom_w; 
            float bloom_uv[8] = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f };
            
            render_color c_bloom = { safe_bloom_alpha_point, col_r, col_g, col_b };
            push_quad(m_quad_verts, bloom_uv, c_bloom);
        }

        float half_w = effwidth * 0.5f;
        m_quad_verts[0] = px0 - half_w; m_quad_verts[1] = py0 - half_w; 
        m_quad_verts[2] = px0 - half_w; m_quad_verts[3] = py0 + half_w; 
        m_quad_verts[4] = px0 + half_w; m_quad_verts[5] = py0 + half_w; 
        m_quad_verts[6] = px0 + half_w; m_quad_verts[7] = py0 - half_w; 
        float core_uv[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
        
        render_color c_core = { core_alpha, col_r, col_g, col_b };
        push_quad(m_quad_verts, is_vector ? core_uv : nullptr, c_core);
        
    } else {
        if (is_vector && enable_bloom) {
            float length_factor = std::min(length / (0.15f * m_height), 1.0f); 
            float dynamic_bloom_mult = (BLOOM_LINE_WIDTH_MULT * (0.5f + 0.5f * length_factor)) + (overbright * BLOOM_OVERBRIGHT_LINE_MULT);
            float bloom_width = effwidth * dynamic_bloom_mult * bloom_scale;
            float half_w = bloom_width * 0.5f;

            float nx = (-dy / length) * half_w; float ny = ( dx / length) * half_w;
            float dx_ext = (dx / length) * half_w; float dy_ext = (dy / length) * half_w;

            float ax0 = px0 + nx; float ay0 = py0 + ny;
            float ax1 = px0 - nx; float ay1 = py0 - ny;
            float bx0 = px1 + nx; float by0 = py1 + ny;
            float bx1 = px1 - nx; float by1 = py1 - ny;

            render_color c_bloom = { safe_bloom_alpha_line, col_r, col_g, col_b };

            // 1. START CAP
            m_quad_verts[0] = ax0 - dx_ext; m_quad_verts[1] = ay0 - dy_ext; 
            m_quad_verts[2] = ax1 - dx_ext; m_quad_verts[3] = ay1 - dy_ext; 
            m_quad_verts[4] = ax1;          m_quad_verts[5] = ay1; 
            m_quad_verts[6] = ax0;          m_quad_verts[7] = ay0; 
            float cap1_uv[8] = { 0.0f, 0.0f,  1.0f, 0.0f,  1.0f, 0.5f,  0.0f, 0.5f };
            push_quad(m_quad_verts, cap1_uv, c_bloom);

            // 2. BODY
            m_quad_verts[0] = ax0; m_quad_verts[1] = ay0; 
            m_quad_verts[2] = ax1; m_quad_verts[3] = ay1; 
            m_quad_verts[4] = bx1; m_quad_verts[5] = by1; 
            m_quad_verts[6] = bx0; m_quad_verts[7] = by0; 
            float body_uv[8] = { 0.0f, 0.5f,  1.0f, 0.5f,  1.0f, 0.5f,  0.0f, 0.5f };
            push_quad(m_quad_verts, body_uv, c_bloom);

            // 3. END CAP
            m_quad_verts[0] = bx0;          m_quad_verts[1] = by0; 
            m_quad_verts[2] = bx1;          m_quad_verts[3] = by1; 
            m_quad_verts[4] = bx1 + dx_ext; m_quad_verts[5] = by1 + dy_ext; 
            m_quad_verts[6] = bx0 + dx_ext; m_quad_verts[7] = by0 + dy_ext; 
            float cap2_uv[8] = { 0.0f, 0.5f,  1.0f, 0.5f,  1.0f, 1.0f,  0.0f, 1.0f };
            push_quad(m_quad_verts, cap2_uv, c_bloom);
        }

        // --- CORE ---
        render_bounds jittered_bounds = { px0, py0, px1, py1 };
        auto [b0, b1] = render_line_to_quad(jittered_bounds, effwidth, 0.0f);
        
        bool use_aa = PRIMFLAG_GET_ANTIALIAS(prim.flags) && !enable_bloom;
        const line_aa_step* step = use_aa ? line_aa_4step : line_aa_1step;
        
        for (; step->weight != 0.0f; step++) {
            render_color c;
            c.a = core_alpha * step->weight; 
            c.r = col_r;
            c.g = col_g;
            c.b = col_b;

            m_quad_verts[0] = b0.x0 + step->xoffs; m_quad_verts[1] = b0.y0 + step->yoffs; 
            m_quad_verts[2] = b0.x1 + step->xoffs; m_quad_verts[3] = b0.y1 + step->yoffs; 
            m_quad_verts[4] = b1.x1 + step->xoffs; m_quad_verts[5] = b1.y1 + step->yoffs; 
            m_quad_verts[6] = b1.x0 + step->xoffs; m_quad_verts[7] = b1.y0 + step->yoffs; 

            float core_uv[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
            push_quad(m_quad_verts, is_vector ? core_uv : nullptr, c);
        }
    }
}

void gles3_renderer::process_quad_primitive(const local_primitive& prim, bool is_screen, int needed_blend)
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
}

void gles3_renderer::render()
{
	float current_time = (float)osd_ticks() / (float)osd_ticks_per_second();	
	
	// ELECTRON GUN STATE (To calculate Dwell Time at the corners) ---
    float prev_x = -9999.0f;
    float prev_y = -9999.0f;
    float prev_dx_norm = 0.0f;
    float prev_dy_norm = 0.0f;	
	
	std::vector<local_primitive> draw_prims;
    std::vector<GLuint> delete_texs;

    {
        std::lock_guard<std::mutex> lock(m_render_mutex);
        draw_prims = m_render_prims; 
        delete_texs = std::move(m_render_textures_to_delete);
        m_render_textures_to_delete.clear();
    }
	
	// Unbind any VAO or Buffer left open by the shader during screen rotation
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    // ------------------------------------
		
    render_bounds v_bounds = { 99999.0f, 99999.0f, -99999.0f, -99999.0f };
    calculate_vector_bounds(draw_prims, v_bounds);
	
	if (m_init)
    {
        //NADA
		m_init = false;
    }

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
    //glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); 
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    //glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
	
	GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] > 0 && viewport[3] > 0)
    {
        m_view_width = viewport[2];
        m_view_height = viewport[3];
    }

	upload_pending_textures(draw_prims);

	glUseProgram(m_quad_program);
	glUniformMatrix4fv(m_uniform_ortho_quad, 1, GL_FALSE, m_ortho.data());

	m_current_texture = 0; 
    m_last_blendmode = -1; 
	
    bool enable_bloom = myosd_get(MYOSD_VECTOR_BLOOM) ? true : false;
	bool require_fbo = m_usefilter || (enable_bloom && BLOOM_FORCE_FBO);

	// HDR CONDITION: We only enable 16-bit memory if the user enables bloom and there are no shaders active
    bool require_hdr = (!m_usefilter && enable_bloom);

	// If the FBO state has mutated (from 8-bit to 16-bit or vice versa), force its recreation
    if (require_hdr != m_fbo_is_hdr) {
        m_fbo_is_hdr = require_hdr;
        m_fbo_dirty = true;
    }

    if (require_fbo && m_fbo_dirty) {
        create_fbo(m_width, m_height, m_fbo_is_hdr);
        m_fbo_dirty = false;
    }	
	
	bool fbo_active = false;

	for (const local_primitive& prim : draw_prims)
	{
        bool is_screen = PRIMFLAG_GET_SCREENTEX(prim.flags);
		bool is_vector = PRIMFLAG_GET_VECTOR(prim.flags);
        
        if (require_fbo && is_vector) {
            if (!fbo_active) {
                flush_batch(); 
                glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
                glViewport(0, 0, m_width, m_height);
				//glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glClearColor(0, 0, 0, 0); 
                glClear(GL_COLOR_BUFFER_BIT);
                fbo_active = true;
            }
        } else if (fbo_active && !is_vector) {
            flush_batch(); 
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
			//glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
            glViewport(0, 0, m_view_width, m_view_height); 
            fbo_active = false;
            draw_vector_fbo(v_bounds); 
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
				// Process magnetic inertia and corner burns
				process_dwell_point(prim, is_vector, enable_bloom, current_time, prev_x, prev_y, prev_dx_norm, prev_dy_norm);					
   
				// Process the main vector line
				process_line_primitive(prim, is_vector, enable_bloom, current_time);
				
				
			} break;

			case render_primitive::QUAD:
			{
				process_quad_primitive(prim, is_screen, needed_blend);
				
			} break;

			case render_primitive::INVALID: break;
		}
	}

	flush_batch();
	
	if (require_fbo && fbo_active) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
		//glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE); 
        glViewport(0, 0, m_view_width, m_view_height);
        draw_vector_fbo(v_bounds);
    }
	
	//glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);	

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

void gles3_renderer::update_texture_cache(const render_primitive& prim, std::shared_ptr<gles_texture>& out_tex)
{
	std::shared_ptr<gles_texture> texture = texture_find(prim, osd_ticks());

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

std::shared_ptr<gles3_renderer::gles_texture> gles3_renderer::texture_create(const render_primitive& prim)
{
	const render_texinfo& texinfo = prim.texture;
    std::shared_ptr<gles_texture> texture = std::make_shared<gles_texture>();
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

std::shared_ptr<gles3_renderer::gles_texture> gles3_renderer::texture_find(const render_primitive& prim, osd_ticks_t now)
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

void gles3_renderer::cleanup_texture_cache()
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
}

//=========================================================
// Texture hashing utilities
//=========================================================

//Copy-pasted from osd/module/render/drawsdl3accel.cpp
static inline HashT texture_compute_hash(const render_texinfo &texture, const u32 flags)
{
	return (HashT)texture.base ^ (flags & (PRIMFLAG_BLENDMODE_MASK | PRIMFLAG_TEXFORMAT_MASK));
}

static bool compare_texture_primitive(const gles_texture& texture, const render_primitive& prim)
{
	//Just compare if the dimensions are the same, we can update the pixel data if they changed
	return texture.texinfo.base == prim.texture.base
		&& texture.texinfo.width     == prim.texture.width
		&& texture.texinfo.height    == prim.texture.height
		&& texture.texinfo.rowpixels == prim.texture.rowpixels
		&& texture.texinfo.palette   == prim.texture.palette
		&& ((texture.prim_flags ^ prim.flags) & (PRIMFLAG_BLENDMODE_MASK | PRIMFLAG_TEXFORMAT_MASK)) == 0;
}

