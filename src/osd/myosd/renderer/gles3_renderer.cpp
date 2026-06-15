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
#include <EGL/egl.h>

// =======================================================================
// GLOBAL HACK EXPORT (From video.cpp / avgdvg.cpp)
// =======================================================================
extern float g_hack_offscreen_overdrive;

#define ANDROID_LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "gles3_renderer", __VA_ARGS__)

/* =======================================================================
 * MAME CORE VECTOR PIPELINE ARCHITECTURE NOTES
 * =======================================================================
 * Based on analysis of MAME's core vector.cpp. These rules define how
 * MAME generates and submits vector primitives to this GLES3 renderer.
 * * 1. INITIAL VECTOR BUFFER QUAD (THE CLEARING QUAD):
 * Before drawing any vectors, MAME always submits a full-screen black
 * rectangle (0.0 to 1.0) with flags: BLENDMODE_ALPHA and VECTORBUF(1).
 * - Purpose: In MAME's software renderer, this acts as the "clear screen"
 * command for the vector layer.
 * - Relevance: When using our custom Dual-FBO pipeline and Painter's
 * Algorithm over background artworks, we must intercept or properly
 * handle this quad so it doesn't accidentally erase the artwork
 * beneath the FBOs with an opaque black layer.
 * * 2. ADDITIVE BLENDING & INTENSITY (ALPHA CHANNEL):
 * Vector lines are ALWAYS submitted with BLENDMODE_ADD (GL_SRC_ALPHA, GL_ONE).
 * - Relevance: The Alpha channel (prim.color.a) does NOT represent
 * standard opacity. It represents the true RAW INTENSITY of the
 * electron beam (mapped from 0 to 255). MAME relies on pure additive
 * blending to overlap lines and naturally build up brightness at vertices.
 * * 3. PRE-CALCULATED BEAM PHYSICS (CPU SIDE):
 * Be aware that MAME already applies several analog physics calculations on
 * the CPU before the primitives reach our renderer. The 'prim.width' and
 * 'prim.color.a' we receive have already been mutated by:
 * - FLICKER: Random intensity drops based on the user's flicker setting.
 * - SIGMOID CURVE: Intensity is passed through a normalized tunable Sigmoid
 * function to realistically simulate phosphor response non-linearity.
 * - DYNAMIC WIDTH: Physical width interpolates between min/max based on intensity.
 * - POINT SCALING: Zero-length lines (dots) are pre-scaled by 'beam_dot_size'.
 * * Conclusion: Our custom HDR/Bloom physics must act as an *optical extension* * of these values, as the CPU has already baked the baseline electrical
 * imperfections into the primitive's geometry and alpha channel.
 * ======================================================================= */

// =======================================================================
// ADVANCED EFFECTS SWITCHES (PHYSICS & OPTICS)
// =======================================================================
// NOTE: For any of these effects to work, the Master Switch
// (MYOSD_VECTOR_ADVANCED_EFFECTS) must be enabled in the emulator settings.

// 1. Vector Bloom / Halo (Optical Scattering)
// Draws the soft light halo around the intense core of the vector.
static bool VECTOR_EFFECT_BLOOM_ENABLED = true;

// 2. Beam Speed Dynamics
// Simulates the electron beam burning the phosphor harder on short strokes (text).
static bool VECTOR_EFFECT_BEAM_DYNAMICS_ENABLED = true;

// 3. Beam Inertia & Dwell Time (Corner Burn)
// Simulates the beam slowing down at sharp corners, causing bright weld points.
static bool VECTOR_EFFECT_CORNER_BURN_ENABLED = true;

// 4. Analog Imperfections (Magnetic Jitter & Noise)
// Adds a lively, chaotic vibration to the vectors, breaking the digital perfection.
static bool VECTOR_EFFECT_BEAM_JITTER_ENABLED = true;

// 5. Phosphor Color Response (Luminance & Bleed)
// Simulates how different colors (Green vs Blue) excite the phosphor with different efficiency.
static bool VECTOR_EFFECT_PHOSPHOR_RESPONSE_ENABLED = false;

// 6. Phosphor Persistence (CRT Trails & Ghosting)
// Enables the Ping-Pong FBO for temporal accumulation and light trails.
static bool VECTOR_EFFECT_PHOSPHOR_PERSISTENCE_ENABLED = true;

// 7. Phosphor Saturation (HDR Overbright)
// Calculates physical expansion when the vector exceeds peak brightness.
static bool VECTOR_EFFECT_OVERBRIGHT_ENABLED = true;

// 8. HDR Auto-Exposure (Eye Adaptation)
// Camera eye-adaptation. Dims the screen dynamically during bright explosions.
static bool VECTOR_EFFECT_AUTO_EXPOSURE_ENABLED = true;

// 9. FBO Half Resolution (Performance Optimization)
// Render optical passes at half resolution. Saves GPU fillrate and provides softness.
static bool VECTOR_FBO_HALF_RES = true;

// 10. Linear Light Physics (Voltage to Photons)
// Converts the raw electrical voltage (0-255 alpha) into physical light energy
// using the CRT's natural Gamma curve (~2.5). Essential for accurate color mixing in both HDR and SDR.
static bool VECTOR_EFFECT_LINEAR_GAMMA = true;

// 11. Raster Fake HDR
// Upgrades standard 8-bit SDR games and artworks to HDR luminance levels using Inverse Tone Mapping.
static bool HDR_RASTER_FAKE_HDR_ENABLED = false;
static float HDR_RASTER_HDR_MULTIPLIER = 2.5f;
static float HDR_RASTER_PAPER_WHITE = 200.0f; // Dedicated standard reference white for raster/artworks
static bool HDR_DIM_VECTOR_ARTWORKS = true;

// 12. Off-screen Monitor Glow
// Simulates the ambient light generated by high-energy beams hitting the inside of the tube.
static bool VECTOR_EFFECT_OFFSCREEN_GLOW_ENABLED = true;
// Multiplier mapped from 0.0f to 1.0f 
static float VECTOR_EFFECT_OFFSCREEN_GLOW_MULT = 0.15f;


// -----------------------------------------------------------------------
// RENDER TARGET OPTIMIZATION (FILLRATE SAVINGS)
// -----------------------------------------------------------------------
// Resolution scale for the FBOs. 0.5f (Half-res) saves 75% of GPU fillrate
// and provides a natural anti-aliasing optical softness to the glowing vectors.
static float VECTOR_EFFECT_FBO_SCALE = 0.5f;

// The absolute minimum vertical resolution the FBO can drop to.
// Prevents the FBO from becoming a pixelated mess on older low-res screens.
constexpr float VECTOR_EFFECT_FBO_MIN_HEIGHT = 480.0f;

// Tracks the TRUE physical scale applied to the FBO, bypassing the UI checkbox.
static float s_active_fbo_scale = 1.0f;

// =======================================================================
// OPTICAL BLOOM CONFIGURATION (DUAL KAWASE)
// =======================================================================

// 1. Kawase Passes (Halo Size)
// - Purpose: How many times the texture is halved. Higher = massive, softer halo.
static int BLOOM_KAWASE_PASSES = 4;

// 2. Brightness Extraction Threshold
// - Purpose: Minimum energy required to emit a halo.
// - Suggested: 1.0f means only pixels that physically exceed standard brightness will glow.
static float BLOOM_KAWASE_THRESHOLD = 0.8f;

// 3. Blur Radius
// - Purpose: The tent filter offset during the upsample phase.
// - Suggested Range: [1.0f - 2.5f]. 1.5f provides smooth, artifact-free light diffusion.
static float BLOOM_KAWASE_RADIUS = 1.25f;

// 4. Optical Intensity
// - Purpose: Final opacity multiplier of the light halo when composed over the core.
static float BLOOM_KAWASE_INTENSITY = 0.22f;

// =======================================================================
// VECTOR CORE GEOMETRY
// =======================================================================
// Physical thickness multiplier of the central laser core for lines.
static float VECTOR_EFFECT_CORE_LINE_WIDTH_MULT = 1.25f;

// Physical thickness multiplier of the central laser core for points/stars.
static float VECTOR_EFFECT_CORE_POINT_WIDTH_MULT = 1.25f;

// Multiplier to specifically boost the light energy of isolated points/stars.
static float VECTOR_EFFECT_POINT_ENERGY_BOOST = 1.0f;


// -----------------------------------------------------------------------
// EXCESS LIGHT PHYSICS (OVERBRIGHT / HDR)
// -----------------------------------------------------------------------

// The absolute maximum limit for extra HDR energy a vector can accumulate.
// - Purpose: Acts as a safety ceiling to prevent the bloom from completely white-washing the screen.
// - Suggested Range: [1.5f - 3.0f] (2.5f allows bright flashes without blinding the player).
static float VECTOR_EFFECT_OVERBRIGHT_MAX = 2.5f;

// How much lines and points physically expand their radius when overloaded with energy.
// - Purpose: Simulates the phosphor bleeding light into adjacent areas when saturated.
// - Suggested Range: [0.30f - 0.70f] (Above 0.8f, the lines will look like fat neon tubes).
static float VECTOR_EFFECT_OVERBRIGHT_LINE_MULT  = 0.55f;
static float VECTOR_EFFECT_OVERBRIGHT_POINT_MULT = 0.45f;

// How much excess energy bleeds into other channels to create white highlights (Crosstalk).
// - Suggested Range: [0.10f - 0.50f] (0.25f creates a natural white core for bright vectors).
static float VECTOR_EFFECT_OVERBRIGHT_CROSSTALK = 0.25f;


// -----------------------------------------------------------------------
// CRT GLOBAL DRIVE (MONITOR VOLTAGE / BRIGHTNESS)
// -----------------------------------------------------------------------

// Global energy multiplier applied to the raw alpha value provided by MAME.
// - Purpose: Simulates turning the "Brightness" or "Drive" knob on the back of the arcade monitor.
// - Suggested Range: [1.0f - 2.0f]
//   -> 1.0f = Dark, accurate, strictly follows MAME's alpha.
//   -> 1.35f = Recommended (Arcade monitor running slightly overdriven).
//   -> 1.8f+ = Extremely bright, almost everything will generate bloom.
static float VECTOR_EFFECT_GLOBAL_DRIVE_MULTIPLIER = 1.35f;

// The physical baseline light of a standard vector (in Nits).
static float VECTOR_EFFECT_BASE_NITS = 300.0f;

// The absolute physical limit of the emulated arcade monitor (in Nits).
static float VECTOR_EFFECT_MAX_NITS = 400.0f;


// =======================================================================
// AUTO-EXPOSURE (HDR EYE ADAPTATION)
// =======================================================================
// Global multiplier to boost the overall brightness of the auto-exposure.
// - 1.0f = Standard dynamic range (1.6f down to 0.7f).
// - 1.20f = Boosts the entire dynamic range by 20% (brighter overall).
// - 0.80f = Dims the entire dynamic range by 20%.
static float VECTOR_EFFECT_AUTO_EXPOSURE_MULT = 1.1f;

// The maximum percentage of the screen area that can be covered by full-intensity
// vectors before the auto-exposure hits its maximum dimming limit (0.7f).
// - Suggested Range: [0.03f - 0.10f]
//   -> 0.05f = 5% of screen area (Good baseline for fast reaction without over-dimming).
static float VECTOR_EFFECT_AUTO_EXPOSURE_THRESHOLD = 0.05f;

// Used only if Auto-Exposure is OFF. Adjusts the baseline brightness.
static float VECTOR_EFFECT_FIXED_EXPOSURE = 1.2f;

// -----------------------------------------------------------------------
// BEAM SPEED PHYSICS (INTENSITY DYNAMICS)
// -----------------------------------------------------------------------

// What percentage of the screen height is considered a "short" line.
// - Purpose: Identifies high-energy strokes like text characters, ships, or small details.
// - Suggested Range: [0.02f - 0.10f] (0.04f = 4% of screen height, perfect for standard text).
constexpr float VECTOR_EFFECT_SHORT_LINE_THRESHOLD_PCT = 0.04f;

// How much extra light energy a tiny line receives.
// - Purpose: Simulates the electron beam burning the phosphor harder because it's moving less distance.
// - Suggested Range: [0.50f - 2.0f] (1.0f = +100% extra energy, makes text highly legible).
static float VECTOR_EFFECT_SHORT_LINE_INTENSITY_BOOST = 1.0f;

// How much the core physically widens when burning the phosphor harder.
// - Purpose: Simulates thermal expansion of the dot on the screen.
// - Suggested Range: [0.10f - 0.40f] (0.20f = +20% thicker core for short lines).
static float VECTOR_EFFECT_SHORT_LINE_WIDTH_BOOST = 0.20f;

// What percentage of the screen height dictates a "short" line for halo compression.
// - Purpose: Shrinks the halo of small elements (like text) so they don't become blurry blobs.
constexpr float VECTOR_EFFECT_HALO_LENGTH_THRESHOLD_PCT = 0.15f;

// =======================================================================
// BEAM INERTIA & DWELL TIME (CORNER BURN)
// =======================================================================

// 1. Angular Threshold (Dot Product)
// - Purpose: How sharp a turn must be to cause the beam to decelerate and burn the corner.
// - Suggested Range: [0.30f - 0.70f].
//   -> 0.50f (60 degrees) triggers burns on sharp polygons like the Asteroids ship.
static float VECTOR_EFFECT_CORNER_DOT_THRESHOLD = 0.50f;

// 2. Corner Burn Intensity Boost
// - Purpose: How much extra energy is dumped into the phosphor during the dwell time.
// - Suggested Range: [1.0f - 3.0f]. (2.0f provides a beautiful glowing weld effect at vertices).
static float VECTOR_EFFECT_CORNER_BURN_BOOST = 1.2f;

// 3. Corner Burn Physical Size
// - Purpose: Physical width of the corner weld. Should be slightly wider than the line itself.
// - Suggested Range: [0.5f - 2.5f]. (1.20f makes it bulge out just slightly from the stroke).
static float VECTOR_EFFECT_CORNER_BURN_WIDTH_MULT = 1.0f;

// -----------------------------------------------------------------------
// ANALOG IMPERFECTIONS (NOISE & MAGNETIC JITTER)
// -----------------------------------------------------------------------

// Maximum physical deviation of the beam due to magnetic coil noise/heat (in pixels).
// - Purpose: Adds a subtle, living vibration to the vectors, breaking the "perfect digital" look.
// - Suggested Range: [0.0f - 0.60f]
//   -> 0.0f = Off (Perfectly stable lines).
//   -> 0.15f = Recommended (Subtle electric hum).
//   -> 0.60f+ = Heavy wear/damaged yoke (Looks like a broken monitor).
static float VECTOR_EFFECT_BEAM_JITTER_AMOUNT = 0.15f;

// Maximum intensity drop caused by voltage fluctuation (Flicker).
// - Purpose: Works with magnetic jitter to create an electrical buzz visible at ANY resolution.
// - Suggested Range: [0.0f - 0.30f] (0.15f = up to 15% brightness drop).
static float VECTOR_EFFECT_BEAM_FLICKER_AMOUNT = 0.15f;

// =======================================================================
// PHOSPHOR COLOR RESPONSE (LUMINANCE & BLEED)
// =======================================================================

// 1. Perceptual Color Weights (Rec.601 / NTSC standard)
// - Purpose: Defines how strongly each color excites the CRT phosphor.
//   Green is highly efficient and bleeds heavily. Blue is inefficient and tight.
constexpr float VECTOR_EFFECT_PHOSPHOR_WEIGHT_R = 0.299f;
constexpr float VECTOR_EFFECT_PHOSPHOR_WEIGHT_G = 0.587f;
constexpr float VECTOR_EFFECT_PHOSPHOR_WEIGHT_B = 0.114f;

// 2. Base Phosphor Response (Floor)
// - Purpose: The minimum energy retained by the darkest/least efficient color (Blue).
// - Suggested Range: [0.30f - 0.50f]. (0.40f ensures blue vectors remain visible).
static float VECTOR_EFFECT_PHOSPHOR_BASE_RESPONSE = 0.40f;

// 3. Luminance Multiplier
// - Purpose: How much the calculated color luminance boosts the final beam energy.
// - Suggested Range: [0.40f - 0.80f]. (0.60f combined with a 0.40f base perfectly caps at 1.0).
static float VECTOR_EFFECT_PHOSPHOR_LUMA_BOOST = 0.60f;

// =======================================================================
// PHOSPHOR PERSISTENCE (CRT TRAILS & GHOSTING)
// =======================================================================

// Decay rate per frame.
// - 0.85f = 15% energy loss per frame (Produces a long, beautiful CRT smear).
// - 0.50f = Fast fade (Subtle ghosting).
static float VECTOR_EFFECT_PHOSPHOR_DECAY = 0.40f;

static gles3_renderer* g_current_renderer = nullptr;

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

static std::pair<render_bounds, render_bounds> render_line_to_quad(float x0, float y0, float x1, float y1, float dx, float dy, float length, float width)
{
    render_bounds b0, b1;
    if (length > 0.0001f)
    {
        float half_width = width * 0.5f;
        float nx = -dy / length * half_width;
        float ny =  dx / length * half_width;

        b0.x0 = x0 + nx;  b0.y0 = y0 + ny;
        b0.x1 = x0 - nx;  b0.y1 = y0 - ny;

        b1.x0 = x1 + nx;  b1.y0 = y1 + ny;
        b1.x1 = x1 - nx;  b1.y1 = y1 - ny;
    }
    else
    {
        b0.x0 = b0.x1 = x0; b0.y0 = b0.y1 = y0;
        b1.x0 = b1.x1 = x1; b1.y0 = b1.y1 = y1;
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

gles3_renderer::gles3_renderer(int width, int height, bool use_hdr_display, float peak_nits)
{
	g_current_renderer = this;
	
	m_use_hdr_display = use_hdr_display; // Store the display mode path
	m_peak_nits = peak_nits;

	__android_log_print(ANDROID_LOG_DEBUG, "gles3_renderer",
        "=== C++ PIPELINE VERIFICATION: SCREEN MODE IS %s ===",
        m_use_hdr_display ? "REAL HDR (10-BIT)" : "STANDARD SDR (8-BIT)");

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

	glUseProgram(m_quad_program);
	GLint quad_hdr_display = glGetUniformLocation(m_quad_program, "u_use_hdr_display");
	glUniform1i(quad_hdr_display, m_use_hdr_display ? 1 : 0);
	glUseProgram(0);

	GLuint hdr_frag_shader = gl_utils::load_shader(hdr_frag_shader_src, GL_FRAGMENT_SHADER);
	m_hdr_program = gl_utils::create_program(quad_vertex_shader, hdr_frag_shader, {
        {0, "a_corner"}, {1, "i_p0p1"}, {2, "i_p2p3"},
        {3, "i_uv0uv1"}, {4, "i_uv2uv3"}, {5, "i_color"}
    });
	glDeleteShader(hdr_frag_shader);
	m_uniform_ortho_hdr = glGetUniformLocation(m_hdr_program, "u_ortho");
	m_uniform_exposure_hdr = glGetUniformLocation(m_hdr_program, "u_exposure");
	m_uniform_use_hdr_display = glGetUniformLocation(m_hdr_program, "u_use_hdr_display");
	m_uniform_peak_nits = glGetUniformLocation(m_hdr_program, "u_device_peak_nits");
	m_uniform_base_nits = glGetUniformLocation(m_hdr_program, "u_base_nits");
	m_uniform_max_nits  = glGetUniformLocation(m_hdr_program, "u_max_nits");	
	m_uniform_offscreen_glow = glGetUniformLocation(m_hdr_program, "u_offscreen_glow");	

	glUseProgram(m_hdr_program);
	glUniform1i(m_uniform_use_hdr_display, m_use_hdr_display ? 1 : 0);
	glUniform1f(m_uniform_peak_nits, m_peak_nits);	
	glUniform1i(glGetUniformLocation(m_hdr_program, "s_texture"), 0);
	glUseProgram(0);
	
	// --- COMPILE KAWASE BLOOM SHADERS ---
	GLuint kd_frag = gl_utils::load_shader(kawase_down_frag_shader_src, GL_FRAGMENT_SHADER);
	m_kawase_down_program = gl_utils::create_program(quad_vertex_shader, kd_frag, {
        {0, "a_corner"}, {1, "i_p0p1"}, {2, "i_p2p3"}, {3, "i_uv0uv1"}, {4, "i_uv2uv3"}, {5, "i_color"}
    });
	glDeleteShader(kd_frag);
	m_loc_kd_ortho = glGetUniformLocation(m_kawase_down_program, "u_ortho");
	m_loc_kd_texel = glGetUniformLocation(m_kawase_down_program, "u_texel_size");
	m_loc_kd_threshold = glGetUniformLocation(m_kawase_down_program, "u_threshold");
	glUseProgram(m_kawase_down_program);
    glUniform1i(glGetUniformLocation(m_kawase_down_program, "s_texture"), 0);	

	GLuint ku_frag = gl_utils::load_shader(kawase_up_frag_shader_src, GL_FRAGMENT_SHADER);
	m_kawase_up_program = gl_utils::create_program(quad_vertex_shader, ku_frag, {
        {0, "a_corner"}, {1, "i_p0p1"}, {2, "i_p2p3"}, {3, "i_uv0uv1"}, {4, "i_uv2uv3"}, {5, "i_color"}
    });
	glDeleteShader(ku_frag);
	m_loc_ku_ortho = glGetUniformLocation(m_kawase_up_program, "u_ortho");
	m_loc_ku_texel = glGetUniformLocation(m_kawase_up_program, "u_texel_size");
	m_loc_ku_radius = glGetUniformLocation(m_kawase_up_program, "u_radius");
	glUseProgram(m_kawase_up_program);
    glUniform1i(glGetUniformLocation(m_kawase_up_program, "s_texture"), 0);
    glUseProgram(0);	

    m_uniform_bloom_hdr = glGetUniformLocation(m_hdr_program, "s_bloom");
    m_uniform_bloom_intensity_hdr = glGetUniformLocation(m_hdr_program, "u_bloom_intensity");
    glUseProgram(m_hdr_program);
    glUniform1i(m_uniform_bloom_hdr, 1);
    glUseProgram(0);	

	//Flag the shader objects for deletion, so they don't leak when the user is switching renderers
	glDeleteShader(quad_vertex_shader);
	glDeleteShader(quad_frag_shader);

	//We're not gonna be compiling shaders anymore, release up the shader compiler resources
	glReleaseShaderCompiler();

	m_uniform_ortho_quad = glGetUniformLocation(m_quad_program, "u_ortho");
	m_uniform_is_vector_quad = glGetUniformLocation(m_quad_program, "u_is_vector");
	
    // --- CACHE UNIFORM LOCATIONS ---
	m_loc_quad_use_hdr = glGetUniformLocation(m_quad_program, "u_use_hdr_display");
    m_loc_quad_raster_fake_hdr = glGetUniformLocation(m_quad_program, "u_raster_fake_hdr");
    m_loc_quad_raster_hdr_mult = glGetUniformLocation(m_quad_program, "u_raster_hdr_mult");
    m_loc_quad_paper_white = glGetUniformLocation(m_quad_program, "u_paper_white");
	m_loc_quad_device_peak = glGetUniformLocation(m_quad_program, "u_device_peak_nits");	

	auto sampler_uniform = glGetUniformLocation(m_quad_program, "s_texture");
	glUseProgram(m_quad_program);
	glUniform1i(sampler_uniform, 0); // Set sampler2D texture unit to 0
    glUseProgram(0);	

	glGenTextures(1, &m_white_texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_white_texture);
	uint32_t white_pixel = 0xFFFFFFFF; // RGBA white
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white_pixel);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// Static base geometry (6 indices to form 2 triangles)
    glGenBuffers(1, &m_corner_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_corner_vbo);
    float corners[6] = {0.0f, 1.0f, 2.0f, 0.0f, 2.0f, 3.0f};
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);

    // Dynamic buffer for instance data
    glGenBuffers(1, &m_instance_vbo);

    m_batch_instances.reserve(4096);
	
	// --- GPU OVERHEAD REDUCTION (VAO) ---
    // Pre-bake the vertex attribute state into a Vertex Array Object (VAO)
    // to save the CPU from validating pointers on every flush_batch() call.
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_corner_vbo);
    glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribDivisor(0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, m_instance_vbo);
    int stride = sizeof(instance_data);

    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(12 * sizeof(float)));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)(16 * sizeof(float)));

    for (int i = 1; i <= 5; i++) {
        glEnableVertexAttribArray(i);
        glVertexAttribDivisor(i, 1); // Instancing instruction
    }

    glBindVertexArray(0); // Save configuration and close
    glBindBuffer(GL_ARRAY_BUFFER, 0);	

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	on_emulatedsize_change(width, height);
}

gles3_renderer::~gles3_renderer()
{
    std::lock_guard<std::mutex> lock(m_render_mutex);
	
    if (g_current_renderer == this) g_current_renderer = nullptr;	
	
	if (eglGetCurrentContext() == EGL_NO_CONTEXT) {
        m_textures_to_delete.clear();
        m_render_textures_to_delete.clear();
        m_texlist.clear();        
        return; 
    }
		
    if (m_quad_program) glDeleteProgram(m_quad_program);
    if (m_hdr_program) glDeleteProgram(m_hdr_program);
		
    if (m_white_texture) glDeleteTextures(1, &m_white_texture);
	
    if (m_corner_vbo) glDeleteBuffers(1, &m_corner_vbo);
    if (m_instance_vbo) glDeleteBuffers(1, &m_instance_vbo);
	
	if (m_kawase_down_program)glDeleteProgram(m_kawase_down_program);
	if (m_kawase_up_program)glDeleteProgram(m_kawase_up_program);
	
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
	
    delete_fbos();
	
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
void gles3_renderer::end_renderer()
{
    m_flush_textures = true;
}

void gles3_renderer::on_emulatedsize_change(int width, int height)
{
    std::lock_guard<std::mutex> lock(m_render_mutex);
	
	m_hdr_fallback_active = false;

	m_ortho = gl_utils::make_ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);
	m_width = width; m_height = height;

    m_last_filter_mode = myosd_get(MYOSD_BITMAP_FILTERING);

    m_flush_textures = true;
    m_filter.set_ortho(m_ortho);

	m_fbo_dirty = true;

	m_init = true;
}

void gles3_renderer::create_fbos(int width, int height, bool need_hdr, bool need_sdr, bool need_filter) {
    // Protect against invalid dimensions induced by screen rotations or minimization
    width = std::max(width, 1);
    height = std::max(height, 1);

    // LAZY INITIALIZATION: We only request GPU memory if the buffer doesn't exist yet.

    // Only allocate the second Ping-Pong FBO if persistence is globally enabled.
    int num_hdr_fbos = VECTOR_EFFECT_PHOSPHOR_PERSISTENCE_ENABLED ? 2 : 1;
    bool hdr_success = true;

    for (int i = 0; i < num_hdr_fbos; i++) {
        if (need_hdr && m_fbo_hdr[i] == 0) {
            glGenTextures(1, &m_fbo_texture_hdr[i]);
            glBindTexture(GL_TEXTURE_2D, m_fbo_texture_hdr[i]);
            
            // 1. IDEAL: RGBA16F
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            glGenFramebuffers(1, &m_fbo_hdr[i]);
            glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_hdr[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fbo_texture_hdr[i], 0);

            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                // 2. FALLBACK R11G11B10F 
                ANDROID_LOG("FP16 FBO failed. Trying hardware-optimized GL_R11F_G11F_B10F fallback...");
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, width, height, 0, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, nullptr);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fbo_texture_hdr[i], 0);

                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    ANDROID_LOG("!!! CRITICAL: All HDR formats failed. Triggering 8-bit SDR Fallback !!!");
                    hdr_success = false;
                    break;
                }
            }
        }
    }
	
	// --- CREATE KAWASE BLOOM MIP-CHAIN ---
    if (VECTOR_EFFECT_BLOOM_ENABLED) {
        int current_w = width / 2;
        int current_h = height / 2;

		// If the main FBO is HDR, the Kawase chain also leverages floating-point formats.
        // If it failed (hdr_success == false), we downgrade the Bloom to 8-bit to prevent crashes.
        GLint internal_format = hdr_success ? GL_R11F_G11F_B10F : GL_RGB;
        GLenum format         = GL_RGB;
        GLenum type           = hdr_success ? GL_UNSIGNED_INT_10F_11F_11F_REV : GL_UNSIGNED_BYTE;

        for (int i = 0; i < MAX_BLOOM_PASSES; i++) {
            if (m_bloom_fbo[i] == 0) {
                current_w = std::max(current_w, 1);
                current_h = std::max(current_h, 1);

                glGenTextures(1, &m_bloom_tex[i]);
                glBindTexture(GL_TEXTURE_2D, m_bloom_tex[i]);
                
                glTexImage2D(GL_TEXTURE_2D, 0, internal_format, current_w, current_h, 0, format, type, nullptr);
                
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                glGenFramebuffers(1, &m_bloom_fbo[i]);
                glBindFramebuffer(GL_FRAMEBUFFER, m_bloom_fbo[i]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloom_tex[i], 0);
            }
            current_w /= 2;
            current_h /= 2;
        }
    }	

    if (!hdr_success) {
        // Cleanup orphans
        for (int j = 0; j < num_hdr_fbos; j++) {
            if (m_fbo_hdr[j]) glDeleteFramebuffers(1, &m_fbo_hdr[j]);
            if (m_fbo_texture_hdr[j]) glDeleteTextures(1, &m_fbo_texture_hdr[j]);
            m_fbo_hdr[j] = 0; m_fbo_texture_hdr[j] = 0;
        }
        m_hdr_fallback_active = true;
        need_sdr = true;
    }

    if (need_sdr && m_fbo_sdr == 0) {
        glGenTextures(1, &m_fbo_texture_sdr);
        glBindTexture(GL_TEXTURE_2D, m_fbo_texture_sdr);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenFramebuffers(1, &m_fbo_sdr);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_sdr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fbo_texture_sdr, 0);
    }
    
    if (need_filter && m_fbo_filter == 0) {
        glGenTextures(1, &m_fbo_texture_filter);
        glBindTexture(GL_TEXTURE_2D, m_fbo_texture_filter);
        
        // EXACT PHYSICAL SIZING FOR SHADERS ---
        // The Filter FBO MUST be a 1:1 mirror of the physical screen (m_view_width x m_view_height).
        // If we use smaller layout sizes, CRT shaders lack the physical pixels
        // to draw their high-res scanlines/masks, and their OutputSize math fails.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_view_width, m_view_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenFramebuffers(1, &m_fbo_filter);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_filter);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fbo_texture_filter, 0);
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void gles3_renderer::delete_fbos() {
	for (int i = 0; i < 2; i++) {
        if (m_fbo_hdr[i]) glDeleteFramebuffers(1, &m_fbo_hdr[i]);
        if (m_fbo_texture_hdr[i]) glDeleteTextures(1, &m_fbo_texture_hdr[i]);
        m_fbo_hdr[i] = 0; m_fbo_texture_hdr[i] = 0;
    }

	m_history_valid = false; // Reset history if FBOs are destroyed
	
	m_current_hdr_fbo = 0;

    if (m_fbo_sdr) glDeleteFramebuffers(1, &m_fbo_sdr);
    if (m_fbo_texture_sdr) glDeleteTextures(1, &m_fbo_texture_sdr);

    m_fbo_sdr = 0; m_fbo_texture_sdr = 0;

    if (m_fbo_filter) glDeleteFramebuffers(1, &m_fbo_filter);
    if (m_fbo_texture_filter) glDeleteTextures(1, &m_fbo_texture_filter);
    m_fbo_filter = 0; m_fbo_texture_filter = 0;
	
	for (int i = 0; i < MAX_BLOOM_PASSES; i++) {
        if (m_bloom_fbo[i]) glDeleteFramebuffers(1, &m_bloom_fbo[i]);
        if (m_bloom_tex[i]) glDeleteTextures(1, &m_bloom_tex[i]);
        m_bloom_fbo[i] = 0;
        m_bloom_tex[i] = 0;
    }
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
                // Protect the Alpha channel so MAME's translucent menus don't
                // punch a hole in the FBO and make the Android window transparent.
				//glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
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

void gles3_renderer::sync_state(const render_primitive_list* primlist, bool in_menu)
{
    //std::lock_guard<std::mutex> lock(m_render_mutex);

    m_in_menu = in_menu;

    // clean old textures
    cleanup_texture_cache();

    static std::vector<local_primitive> temp_prims;

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
		
		bool is_screen = PRIMFLAG_GET_SCREENTEX(prim.flags);
        bool is_vector = PRIMFLAG_GET_VECTOR(prim.flags);
        
		lp.is_artwork = (!is_screen && !is_vector && prim.type == render_primitive::QUAD);	

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
		std::string trace_info = "";
		int static_count = 0, dynamic_count = 0;

		for (const auto& tex : m_texlist) {
			bool is_dynamic = (tex->base_back != nullptr);
			is_dynamic ? dynamic_count++ : static_count++;
			char buf[64];
			snprintf(buf, sizeof(buf), "[ID:%u %dx%d %s]",
					 tex->texture_id, tex->texinfo.width, tex->texinfo.height,
					 is_dynamic ? "DYN" : "STA");
			trace_info += buf;
        }
		ANDROID_LOG("TOTAL CACHE -> Elements: %zu (Static: %d | Dynamic: %d) Info: %s", m_texlist.size(), static_count, dynamic_count, trace_info.c_str());
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

    // 1. Bind our pre-baked VAO (Saves dozens of CPU state calls)
    glBindVertexArray(m_vao);

    // 2. Upload massive data to GPU without stalling
    glBindBuffer(GL_ARRAY_BUFFER, m_instance_vbo);
    size_t data_size = m_batch_instances.size() * sizeof(instance_data);
    
    // Explicit Orphaning: Prevent driver stalls
    glBufferData(GL_ARRAY_BUFFER, data_size, nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, data_size, m_batch_instances.data());

    // 3. DRAW EVERYTHING AT ONCE! (6 corners per instance)
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, m_batch_instances.size());

    // 4. Quick cleanup
    glBindVertexArray(0);
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

				//GLint internal_format = m_use_hdr_display ? GL_SRGB8_ALPHA8 : GL_RGBA;
				//glTexImage2D(GL_TEXTURE_2D, 0, internal_format, prim.texture->texinfo.width, prim.texture->texinfo.height, 0, GL_RGBA, //GL_UNSIGNED_BYTE, prim.upload_ptr);

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


// #define DEBUG_VECTOR_PHYSICS

bool gles3_renderer::calculate_auto_exposure(const std::vector<local_primitive>& draw_prims)
{
    bool has_vectors = false;
    float scene_energy = 0.0f;
    
    // max_final must remain outside the macro because 
    // it is actively used in the Highlight Preservation logic.
    float max_final = 0.0f; 

#ifdef DEBUG_VECTOR_PHYSICS
    // --- Detailed Energy Tracking Metrics ---
    float min_base = 9999.0f, max_base = 0.0f, sum_base = 0.0f;
    float min_final = 9999.0f, sum_final = 0.0f;
    float min_ob = 9999.0f, max_ob = 0.0f, sum_ob = 0.0f;
    
    // --- Telemetry Accumulators ---
    float sum_dyn_added = 0.0f;   // Tracks pure energy added by Beam Dynamics
    float total_weight_area = 0.0f;
    int vector_count = 0;
#endif

    // Convert logical MAME layout units to physical screen pixels.
    // This prevents the Pythagorean theorem from stretching/distorting line lengths
    // when the aspect ratio is non-square or the device is rotated.
    float scale_x = (float)m_view_width / (float)std::max(m_width, 1);
    float scale_y = (float)m_view_height / (float)std::max(m_height, 1);
    
    // We use the average scale to approximate the physical thickness of the vector core
    float scale_avg = (scale_x + scale_y) * 0.5f;

    for (const auto& prim : draw_prims) {
        if (PRIMFLAG_GET_VECTOR(prim.flags)) {
            has_vectors = true;

#ifdef DEBUG_VECTOR_PHYSICS
            vector_count++;
#endif

            // Map logical distances to physical screen pixels BEFORE calculating length
            float phys_dx = (prim.bounds.x1 - prim.bounds.x0) * scale_x;
            float phys_dy = (prim.bounds.y1 - prim.bounds.y0) * scale_y;
            float phys_len = std::sqrt(phys_dx * phys_dx + phys_dy * phys_dy);

            if (phys_len < 0.001f) continue;

            // Scale the geometric width to physical pixels as well
            float phys_width = std::max(prim.width * scale_avg, 0.01f);

            // 1. Raw MAME intensity (Voltage from 0.0 to 1.0)
            float base_intensity = prim.color.a;

            // --- CRT ELECTRON GUN PHYSICS (Voltage to Linear Light) ---
            if (VECTOR_EFFECT_AUTO_EXPOSURE_ENABLED && VECTOR_EFFECT_LINEAR_GAMMA) {
                // OPTIMIZATION: std::pow(x, 2.5f) is brutally slow on CPU.
                // We use the hardware-accelerated mathematical equivalence x^2.5 = x^2 * sqrt(x).
                base_intensity = (base_intensity * base_intensity) * std::sqrt(base_intensity);
            }

            // --- FAITHFUL PHYSICS PRE-CALCULATION ---
            float phosphor_response = 1.0f;
            if (VECTOR_EFFECT_PHOSPHOR_RESPONSE_ENABLED) {
                float luminance = (prim.color.r * VECTOR_EFFECT_PHOSPHOR_WEIGHT_R) + (prim.color.g * VECTOR_EFFECT_PHOSPHOR_WEIGHT_G) + (prim.color.b * VECTOR_EFFECT_PHOSPHOR_WEIGHT_B);
                phosphor_response = VECTOR_EFFECT_PHOSPHOR_BASE_RESPONSE + (luminance * VECTOR_EFFECT_PHOSPHOR_LUMA_BOOST);
            }

            float simulated_energy = base_intensity * VECTOR_EFFECT_GLOBAL_DRIVE_MULTIPLIER * phosphor_response;

            // Essential for Highlight Preservation below
            max_final = std::max(max_final, simulated_energy);

#ifdef DEBUG_VECTOR_PHYSICS
            // --- TRACK PRE-DYNAMICS STATE ---
            float pre_dyn_energy = simulated_energy;
            float pre_dyn_width = phys_width;
#endif

            // 2. Beam Dynamics (Short Line Boost)
            if (VECTOR_EFFECT_BEAM_DYNAMICS_ENABLED) {
                // Threshold must also be evaluated in physical pixels
                float threshold = (float)m_view_height * VECTOR_EFFECT_SHORT_LINE_THRESHOLD_PCT;
                if (phys_len < threshold && phys_len > 0.1f) {
                    float shortness = 1.0f - (phys_len / threshold);
                    simulated_energy *= (1.0f + shortness * VECTOR_EFFECT_SHORT_LINE_INTENSITY_BOOST);
                    phys_width *= (1.0f + shortness * VECTOR_EFFECT_SHORT_LINE_WIDTH_BOOST);
                }
            }

#ifdef DEBUG_VECTOR_PHYSICS
            // --- CALCULATE BEAM DYNAMICS CONTRIBUTION ---
            float dyn_energy_delta = (simulated_energy * phys_width) - (pre_dyn_energy * pre_dyn_width);
            sum_dyn_added += dyn_energy_delta * phys_len; 

            // 3. Overbright Physical Expansion (Telemetry Only)
            // OPTIMIZATION: Calculating this std::pow inside the loop without the #ifdef 
            // caused massive CPU overhead, as it wasn't even being added to scene_energy.
            float overbright_raw = std::max(simulated_energy - 1.0f, 0.0f);
            float overbright = std::min(std::pow(overbright_raw, 0.7f), VECTOR_EFFECT_OVERBRIGHT_MAX);
#endif

            // 4. PRECISE OPTICAL ENERGY INTEGRAL ---
            // A. Core Energy (The physical laser beam)
            float core_area = phys_len * phys_width;
            float core_energy_total = core_area * simulated_energy;
            
            scene_energy += core_energy_total;

#ifdef DEBUG_VECTOR_PHYSICS
            // --- METRICS COLLECTION ---
            min_base = std::min(min_base, base_intensity);
            max_base = std::max(max_base, base_intensity);
            sum_base += base_intensity * core_area;

            min_final = std::min(min_final, simulated_energy);
            sum_final += simulated_energy * core_area;

            min_ob = std::min(min_ob, overbright);
            max_ob = std::max(max_ob, overbright);
            sum_ob += overbright * core_area;

            total_weight_area += core_area;
#endif
        }
    }

    if (has_vectors) {
        // 5. RESOLUTION-INDEPENDENT 2D NORMALIZATION
        // USE PHYSICAL VIEWPORT AREA ---
        float screen_area = (float)(std::max(m_view_width, 1) * std::max(m_view_height, 1));
        float safe_threshold = std::max(VECTOR_EFFECT_AUTO_EXPOSURE_THRESHOLD, 0.001f);
        float normalized_energy = scene_energy / (screen_area * safe_threshold);

        // --- AUTO-EXPOSURE (Eye Adaptation Logic) ---
        if (VECTOR_EFFECT_AUTO_EXPOSURE_ENABLED) {

            // Highlight Preservation (Protects against hard clipping of extreme peaks)
            float highlight_preservation = std::clamp((max_final - 1.0f) * 0.05f, 0.0f, 0.25f);

            // Base exposure is 1.6f for dark games (Asteroids).
            // Subtract normalized energy and highlight preservation factor.
            float target_exposure = std::clamp(1.6f - normalized_energy - highlight_preservation, 0.7f, 1.6f);
            target_exposure *= VECTOR_EFFECT_AUTO_EXPOSURE_MULT;

            // Temporal Smoothing (Moving Average)
            float adaptation_speed = (target_exposure < m_current_exposure) ? 0.3f : 0.02f;
            m_current_exposure += (target_exposure - m_current_exposure) * adaptation_speed;
        } else {
            m_current_exposure = VECTOR_EFFECT_FIXED_EXPOSURE;
        }

#ifdef DEBUG_VECTOR_PHYSICS
        // --- DEBUG LOGGING (Trace every 1 second) ---
        static osd_ticks_t last_log_time = 0;
        osd_ticks_t current_ticks = osd_ticks();

        if (current_ticks - last_log_time >= osd_ticks_per_second()) {
            
            // Calculate weighted averages safely
            float avg_base = (total_weight_area > 0.0f) ? (sum_base / total_weight_area) : 0.0f;
            float avg_final = (total_weight_area > 0.0f) ? (sum_final / total_weight_area) : 0.0f;
            float avg_ob = (total_weight_area > 0.0f) ? (sum_ob / total_weight_area) : 0.0f;

            // Safety check to avoid division by zero
            float safe_scene_energy = std::max(scene_energy, 0.0001f);
            float pct_dyn = (sum_dyn_added / safe_scene_energy) * 100.0f;

            ANDROID_LOG("=== VECTOR ENERGY METRICS (Vectors: %d) ===", vector_count);
            ANDROID_LOG("  -> Base Intensity | Min: %.3f, Avg: %.3f, Max: %.3f", min_base, avg_base, max_base);
            ANDROID_LOG("  -> Final Physics  | Min: %.3f, Avg: %.3f, Max: %.3f", min_final, avg_final, max_final);
            ANDROID_LOG("  -> Overbright     | Min: %.3f, Avg: %.3f, Max: %.3f", min_ob, avg_ob, max_ob);
            
            // --- TELEMETRY READOUT ---
            ANDROID_LOG("  -> Beam Dynamics Added Energy : %.2f%% of total Scene Energy", pct_dyn);

            if (VECTOR_EFFECT_AUTO_EXPOSURE_ENABLED) {
                ANDROID_LOG("  -> Auto-Exposure  | Scene Norm: %.4f | Current Exp: %.3f", normalized_energy, m_current_exposure);
            } else {
                ANDROID_LOG("  -> Auto-Exposure  | DISABLED | Fixed Exp: %.3f", m_current_exposure);
            }
            ANDROID_LOG("===========================================");
            last_log_time = current_ticks;
        }
#endif
    }

    return has_vectors;
}


void gles3_renderer::resolve_hdr(GLuint target_fbo, float layout_w, float layout_h,
	const render_bounds& layout_bounds, const std::array<float, 16>& vector_ortho)
{
    // =================================================================
    // DUAL KAWASE BLOOM PIPELINE (POST-PROCESSING)
    // =================================================================
    int base_passes = std::min(BLOOM_KAWASE_PASSES, MAX_BLOOM_PASSES);
    int passes = base_passes;

    // --- RESOLUTION DEPENDENCY COMPENSATION (MIP-LEVEL DEPTH) ---
    // If the FBO is running near Full Resolution (scale > 0.9), it's twice as large
    // as a Half-Res FBO. To achieve the exact same optical spread without breaking 
    // the tent filter with a massive radius, we must add one extra pass.
    if (s_active_fbo_scale > 0.9f && passes < MAX_BLOOM_PASSES) {
        passes += 1;
    }

    if (VECTOR_EFFECT_BLOOM_ENABLED && passes > 0 && m_bloom_fbo[0] != 0) {
        
        glDisable(GL_BLEND); // Full overwrite for downsampling
        
        // --- 1. DOWNSAMPLE PHASE (Extract light & blur) ---
        glUseProgram(m_kawase_down_program);
        
        int current_w = (int)layout_w / 2;
        int current_h = (int)layout_h / 2;
		GLuint current_source_tex = m_hdr_fallback_active ? m_fbo_texture_sdr : m_fbo_texture_hdr[m_current_hdr_fbo];
        
        for (int i = 0; i < passes; i++) {
            current_w = std::max(current_w, 1);
            current_h = std::max(current_h, 1);
            
            glBindFramebuffer(GL_FRAMEBUFFER, m_bloom_fbo[i]);
            glViewport(0, 0, current_w, current_h);
            
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, current_source_tex);
            
            glUniform2f(m_loc_kd_texel, 1.0f / (float)(current_w * 2), 1.0f / (float)(current_h * 2));
            glUniform1f(m_loc_kd_threshold, (i == 0) ? BLOOM_KAWASE_THRESHOLD : 0.0f);
            
            // Build dynamic ortho for this FBO level
            float fw = (float)current_w; float fh = (float)current_h;
            std::array<float, 16> exact_ortho = gl_utils::make_ortho(0.0f, fw, fh, 0.0f, -1.0f, 1.0f);
            glUniformMatrix4fv(m_loc_kd_ortho, 1, GL_FALSE, exact_ortho.data());
            
            float fbo_verts[8] = { 0.0f, 0.0f, 0.0f, fh, fw, fh, fw, 0.0f };
            float fbo_uv[8] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f };
            render_color white = { 1.0f, 1.0f, 1.0f, 1.0f };
            
            push_quad(fbo_verts, fbo_uv, white);
            flush_batch();
            
            current_source_tex = m_bloom_tex[i];
            current_w /= 2;
            current_h /= 2;
        }

		// --- 2. UPSAMPLE PHASE (Diffuse and expand light) ---
        glUseProgram(m_kawase_up_program);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE); // Additive blending as we climb back up
        
        // --- DEVICE RESOLUTION INDEPENDENCE ---
        // We calculate the TRUE screen size from layout_bounds, bypassing the FBO dimensions
        // to ensure the res_scale doesn't accidentally shrink when Half-Res is enabled.
        float true_screen_w = std::max(layout_bounds.x1 - layout_bounds.x0, 1.0f);
        float true_screen_h = std::max(layout_bounds.y1 - layout_bounds.y0, 1.0f);
        float shortest_side = std::min(true_screen_w, true_screen_h);
        
        float res_scale = shortest_side / 1080.0f;
        float dynamic_radius = BLOOM_KAWASE_RADIUS * res_scale;
        
        for (int i = passes - 2; i >= 0; i--) {
            int target_w = std::max((int)layout_w / (1 << (i + 1)), 1);
            int target_h = std::max((int)layout_h / (1 << (i + 1)), 1);
            
            int source_w = std::max((int)layout_w / (1 << (i + 2)), 1);
            int source_h = std::max((int)layout_h / (1 << (i + 2)), 1);
            
            glBindFramebuffer(GL_FRAMEBUFFER, m_bloom_fbo[i]);
            glViewport(0, 0, target_w, target_h);
            
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_bloom_tex[i + 1]); 
            
            glUniform2f(m_loc_ku_texel, 1.0f / (float)source_w, 1.0f / (float)source_h);
            
            // Apply the dynamically scaled radius to maintain physical proportions
            glUniform1f(m_loc_ku_radius, dynamic_radius);
            
            float fw = (float)target_w; float fh = (float)target_h;
            std::array<float, 16> exact_ortho = gl_utils::make_ortho(0.0f, fw, fh, 0.0f, -1.0f, 1.0f);
            glUniformMatrix4fv(m_loc_ku_ortho, 1, GL_FALSE, exact_ortho.data());
            
            float fbo_verts[8] = { 0.0f, 0.0f, 0.0f, fh, fw, fh, fw, 0.0f };
            float fbo_uv[8] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f };
            render_color white = { 1.0f, 1.0f, 1.0f, 1.0f };
            
            push_quad(fbo_verts, fbo_uv, white);
            flush_batch();
        }
    }

    // =================================================================
    // FINAL HDR TONE-MAPPING COMPOSITION
    // =================================================================
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glUseProgram(m_hdr_program);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE); // Anti-Fattening
	
    glUniform1f(m_uniform_exposure_hdr, m_current_exposure);
	glUniform1f(m_uniform_base_nits, VECTOR_EFFECT_BASE_NITS);
    glUniform1f(m_uniform_max_nits,  VECTOR_EFFECT_MAX_NITS);
	
	if (m_uniform_offscreen_glow != -1) {
		glUniform1f(m_uniform_offscreen_glow, m_current_monitor_glow);
	}	

    // Bind Core HDR Buffer to Texture Unit 0
	m_current_texture = m_fbo_texture_hdr[m_current_hdr_fbo];
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_current_texture);

	// Bind the new Bloom Halo Buffer to Texture Unit 1
    glActiveTexture(GL_TEXTURE1);
    if (VECTOR_EFFECT_BLOOM_ENABLED && passes > 0 && m_bloom_tex[0] != 0) {
        glBindTexture(GL_TEXTURE_2D, m_bloom_tex[0]);
        
		// --- ENERGY NORMALIZATION (Perceptual Spatial Diffusion) ---
        // Additive Kawase upsampling accumulates energy with every pass. 
        // We normalize the final intensity, but since spatial light diffusion 
        // covers a 2D area (quadratic), a linear division dims the core too much.
        // We use a square root curve to preserve the punchy hot-spot of the laser.
        float ratio = (float)base_passes / (float)passes;
        //float intensity_comp = std::sqrt(ratio);
		float intensity_comp = std::pow(ratio, 0.75f);
        glUniform1f(m_uniform_bloom_intensity_hdr, BLOOM_KAWASE_INTENSITY * intensity_comp);
    } else {
        glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1f(m_uniform_bloom_intensity_hdr, 0.0f);
    }
    // Return active unit to 0 for standard ops
    glActiveTexture(GL_TEXTURE0); 

    render_color white = { 1.0f, 1.0f, 1.0f, 1.0f };

    if (target_fbo == 0) {
        // Resolving directly to Screen: Use global mobile viewports and screen ortho matrix
        glViewport(0, 0, m_view_width, m_view_height);
        glUniformMatrix4fv(m_uniform_ortho_hdr, 1, GL_FALSE, m_ortho.data());
		// Update display path uniform based on target surface capabilities
        glUniform1i(m_uniform_use_hdr_display, m_use_hdr_display ? 1 : 0);

        float fbo_verts[8] = { layout_bounds.x0, layout_bounds.y0, layout_bounds.x0, layout_bounds.y1, layout_bounds.x1, layout_bounds.y1, layout_bounds.x1, layout_bounds.y0 };
        float fbo_uv[8] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f };
        
        push_quad(fbo_verts, fbo_uv, white);
        flush_batch();
    } else {
        // --- KALEIDOSCOPE FIX (FRACTIONAL PIXELS) ---
        // Resolving to SDR FBO: We must use a pure integer orthographic matrix
        // to ensure a perfect 1:1 pixel copy. Using layout_bounds and vector_ortho
        // introduces floating-point sub-pixel resampling that causes kaleidoscope/blur artifacts.
        glViewport(0, 0, (GLsizei)layout_w, (GLsizei)layout_h);
        
        float fw = (float)(int)layout_w;
        float fh = (float)(int)layout_h;
        std::array<float, 16> exact_ortho = gl_utils::make_ortho(0.0f, fw, fh, 0.0f, -1.0f, 1.0f);
        glUniformMatrix4fv(m_uniform_ortho_hdr, 1, GL_FALSE, exact_ortho.data());
        glUniform1i(m_uniform_use_hdr_display, 0);

        float exact_verts[8] = { 0.0f, 0.0f, 0.0f, fh, fw, fh, fw, 0.0f };
        float fbo_uv[8] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f };
        
        push_quad(exact_verts, fbo_uv, white);
        flush_batch();
    }

    // CRITICAL: Only clear the FBO if persistence is disabled or multi-monitor is active.
    // If persistence is ON, we MUST keep the accumulated light in the FBO for the next frame's Ping-Pong copy!
    if (!VECTOR_EFFECT_PHOSPHOR_PERSISTENCE_ENABLED || m_multi_monitor_detected) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_hdr[m_current_hdr_fbo]);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    // Restore state
    glUseProgram(m_quad_program);
    m_current_texture = 0;
    m_last_blendmode = -1;
}

void gles3_renderer::switch_fbo_target(int target_fbo, int& current_fbo, bool require_sdr,
	float layout_w, float layout_h, const render_bounds& layout_bounds, const std::array<float, 16>& vector_ortho, bool has_vectors)
{
    // If we are already in the correct FBO, do nothing
    if (current_fbo == target_fbo) return;

    // Flush any pending geometry before switching contexts
    flush_batch();

    // --- PIPELINE UNWINDING (Cascading Resolve) ---

    // 1. Leaving Filter FBO (Background Game Art) -> Draw to Screen with HDR
    if (current_fbo == 3) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_view_width, m_view_height);
        glUseProgram(m_quad_program);
        glUniformMatrix4fv(m_uniform_ortho_quad, 1, GL_FALSE, m_ortho.data());
        
        // Apply the HDR settings using the separated raster paper white uniform
		glUniform1i(m_loc_quad_use_hdr, m_use_hdr_display ? 1 : 0);
        glUniform1i(m_loc_quad_raster_fake_hdr, (HDR_RASTER_FAKE_HDR_ENABLED && !has_vectors) ? 1 : 0);
        glUniform1f(m_loc_quad_raster_hdr_mult, HDR_RASTER_HDR_MULTIPLIER);
        glUniform1f(m_loc_quad_paper_white, HDR_RASTER_PAPER_WHITE);
		glUniform1f(m_loc_quad_device_peak, m_peak_nits);		
        glUniform1i(m_uniform_is_vector_quad, 0); 
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_fbo_texture_filter);

        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        
        render_color white = { 1.0f, 1.0f, 1.0f, 1.0f };
        
        // -- FULL SCREEN BLIT ---
        // Since m_fbo_filter is a 1:1 physical mirror of the screen, we blit it using 
        // the full logical coordinates of the screen matrix (0 to m_width/m_height).
        float full_verts[8] = { 0.0f, 0.0f, 0.0f, (float)m_height, (float)m_width, (float)m_height, (float)m_width, 0.0f };
        float fbo_uv[8] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f };
        
        push_quad(full_verts, fbo_uv, white);
        flush_batch();
        
        m_current_texture = 0;
        m_last_blendmode = -1;
        m_last_is_vector = -1;
        
        // Clear Filter FBO for reuse
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_filter);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        current_fbo = 0;
    }

    // 2. Leaving HDR Vector FBO -> Resolve its light down to SDR
    if (current_fbo == 2) {
        GLuint resolve_target = require_sdr ? m_fbo_sdr : 0;
        resolve_hdr(resolve_target, layout_w, layout_h, layout_bounds, vector_ortho);
        current_fbo = require_sdr ? 1 : 0; 
    }

    // 3. Leaving SDR Vector FBO -> Draw vectors to Screen
    if (current_fbo == 1 && target_fbo == 0) {
        float fbo_verts[8] = { layout_bounds.x0, layout_bounds.y0, layout_bounds.x0, layout_bounds.y1, layout_bounds.x1, layout_bounds.y1, layout_bounds.x1, layout_bounds.y0 };
        float fbo_uv[8] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f }; 
        
        render_color white = { 1.0f, 1.0f, 1.0f, 1.0f };

        if (m_use_hdr_display && m_usefilter) {
            // Apply filter over the VECTORS before drawing to screen (HDR Path)
            glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_filter);
            
            // Act as a 1:1 screen mirror
            glViewport(0, 0, m_view_width, m_view_height);
            m_filter.set_ortho(m_ortho);
            
            glEnable(GL_BLEND); 
            glBlendFunc(GL_ONE, GL_ZERO); 
            
            // Pass physical dimensions to ensure OutputSize is correct. 
            m_filter.draw_quad(m_fbo_texture_sdr, fbo_verts, fbo_uv, (int)layout_w, (int)layout_h, m_view_width, m_view_height);
            
			// Resolve the FULL FBO to the screen using the HDR quad
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, m_view_width, m_view_height);
            glUseProgram(m_quad_program);
            
            glUniformMatrix4fv(m_uniform_ortho_quad, 1, GL_FALSE, m_ortho.data());
            glUniform1i(m_loc_quad_use_hdr, 1);
            glUniform1i(m_loc_quad_raster_fake_hdr, (HDR_RASTER_FAKE_HDR_ENABLED && !has_vectors) ? 1 : 0);
            glUniform1f(m_loc_quad_raster_hdr_mult, HDR_RASTER_HDR_MULTIPLIER);
            glUniform1f(m_loc_quad_paper_white, HDR_RASTER_PAPER_WHITE);
            glUniform1i(m_uniform_is_vector_quad, 0); 
            
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_fbo_texture_filter);

            float full_verts[8] = { 0.0f, 0.0f, 0.0f, (float)m_height, (float)m_width, (float)m_height, (float)m_width, 0.0f };
            
            glEnable(GL_BLEND);
            if (has_vectors) glBlendFunc(GL_ONE, GL_ONE); // Pure Additive over background
            else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

            push_quad(full_verts, fbo_uv, white);
            flush_batch();
        } else {
            // Standard SDR Path for vectors
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, m_view_width, m_view_height);
            glUseProgram(m_quad_program);
            glUniformMatrix4fv(m_uniform_ortho_quad, 1, GL_FALSE, m_ortho.data());
            
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_fbo_texture_sdr);
            
            if (m_usefilter) {
                m_filter.draw_quad(m_fbo_texture_sdr, fbo_verts, fbo_uv, (int)layout_w, (int)layout_h, m_view_width, m_view_height);
                glUseProgram(m_quad_program);
            }

            glEnable(GL_BLEND);
            if (has_vectors) glBlendFunc(GL_ONE, GL_ONE); // Pure Additive over background
            else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

            if (!m_usefilter) {
                push_quad(fbo_verts, fbo_uv, white);
                flush_batch();
            }
        }

        m_current_texture = 0; 
        m_last_blendmode = -1;

        // Clear SDR FBO for next frame
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_sdr);
        glClearColor(0.0f, 0.0f, 0.0f, has_vectors ? 0.1f : 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    // --- PIPELINE SETUP (Binding the requested FBO) ---

    if (target_fbo == 3) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_filter);
        glViewport(0, 0, m_view_width, m_view_height); 
        glUseProgram(m_quad_program);
        glUniformMatrix4fv(m_uniform_ortho_quad, 1, GL_FALSE, m_ortho.data());
        glUniform1i(m_loc_quad_use_hdr, 0);
        glUniform1i(m_loc_quad_raster_fake_hdr, 0); 
    } else if (target_fbo == 2) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_hdr[m_current_hdr_fbo]);
        glViewport(0, 0, (GLsizei)layout_w, (GLsizei)layout_h);
        glUseProgram(m_quad_program);
        glUniformMatrix4fv(m_uniform_ortho_quad, 1, GL_FALSE, vector_ortho.data());
    } else if (target_fbo == 1) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_sdr);
        glViewport(0, 0, (GLsizei)layout_w, (GLsizei)layout_h);
        glUseProgram(m_quad_program);
        glUniformMatrix4fv(m_uniform_ortho_quad, 1, GL_FALSE, vector_ortho.data());
        glUniform1i(m_loc_quad_use_hdr, 0);     
        glUniform1i(m_loc_quad_raster_fake_hdr, 0); 
	} else if (target_fbo == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_view_width, m_view_height);
        glUseProgram(m_quad_program);
        glUniformMatrix4fv(m_uniform_ortho_quad, 1, GL_FALSE, m_ortho.data());
        glUniform1i(m_loc_quad_use_hdr, m_use_hdr_display ? 1 : 0);
        glUniform1i(m_loc_quad_raster_fake_hdr, (HDR_RASTER_FAKE_HDR_ENABLED && !has_vectors) ? 1 : 0);
        glUniform1f(m_loc_quad_raster_hdr_mult, HDR_RASTER_HDR_MULTIPLIER);
        glUniform1f(m_loc_quad_paper_white, HDR_RASTER_PAPER_WHITE);
        glUniform1f(m_loc_quad_device_peak, m_peak_nits);		
    }

    current_fbo = target_fbo;
}

void gles3_renderer::process_dwell_point(const local_primitive& prim, bool is_vector, bool enable_advanced_effects, float current_time, float& prev_x, float& prev_y, float& prev_dx_norm, float& prev_dy_norm)
{
	// Early exit if the specific effect or the master switch is disabled
    if (!VECTOR_EFFECT_CORNER_BURN_ENABLED || !is_vector || !enable_advanced_effects) {
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

        // --- RESOLUTION INDEPENDENCE (Connection Threshold) ---
        // At 480p, 1.0 pixel is a safe threshold to consider two lines connected.
        // On modern high-DPI screens (1080p+), floating point math can cause connected
        // vectors to be slightly further apart. We scale the threshold to match!
        float scale_factor = (float)std::max(m_height, 1) / 480.0f;
        float connect_threshold = 1.0f * scale_factor;

        // If the current line starts where the previous one ended (continuous stroke)
        if (std::abs(px0 - prev_x) < connect_threshold && std::abs(py0 - prev_y) < connect_threshold) {

			// Dot Product to find the turn angle
            float dot = (prev_dx_norm * dx_norm) + (prev_dy_norm * dy_norm);

            // If the turn is sharper than our threshold
            if (dot < VECTOR_EFFECT_CORNER_DOT_THRESHOLD) {

                // Calculate brake aggressiveness (0.0 to 1.0)
                float sharpness = (VECTOR_EFFECT_CORNER_DOT_THRESHOLD - dot) / (VECTOR_EFFECT_CORNER_DOT_THRESHOLD + 1.0f);

                // INJECT A "DWELL POINT" (Corner Burn)
                local_primitive corner_prim = prim;
                corner_prim.bounds.x0 = px0; corner_prim.bounds.x1 = px0;
                corner_prim.bounds.y0 = py0; corner_prim.bounds.y1 = py0;

                // Confine the light physically inside the vector
                corner_prim.width = prim.width * VECTOR_EFFECT_CORNER_BURN_WIDTH_MULT;

                // Boost the point's energy based on turn sharpness
                float energy_boost = 1.0f + (sharpness * VECTOR_EFFECT_CORNER_BURN_BOOST);

				// --- TRUE HDR PHYSICS & INVERSE GAMMA COMPENSATION ---
				if (enable_advanced_effects && VECTOR_EFFECT_LINEAR_GAMMA) {
					// 'prim.color.a' stores VOLTAGE, but our boost is calculated in physical LIGHT ENERGY.
					// We must apply the inverse of the CRT Gamma (1.0 / 2.5 = 0.4) to the boost 
					// before multiplying, so it scales correctly when converted later in process_line_primitive.
					float voltage_boost = std::pow(energy_boost, 0.4f);
					corner_prim.color.a = prim.color.a * voltage_boost;
				} else {
					// Standard linear multiplier for legacy pipelines
					corner_prim.color.a = prim.color.a * energy_boost;
				}

                // Draw the bright point before drawing the actual line
				process_line_primitive(corner_prim, is_vector, enable_advanced_effects, current_time, true);
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

void gles3_renderer::apply_phosphor_persistence(float fbo_w, float fbo_h)
{
    int next_hdr_fbo = 1 - m_current_hdr_fbo;
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_hdr[next_hdr_fbo]);

    // --- PING-PONG COPY WITH DECAY ---
    glDisable(GL_BLEND);
    glViewport(0, 0, (GLsizei)fbo_w, (GLsizei)fbo_h);

    glUseProgram(m_quad_program);

    // --- KALEIDOSCOPE FIX (FRACTIONAL PIXELS) ---
    // We use a pure integer orthographic matrix to ensure a perfect 1:1 texture copy.
    float fw = (float)(int)fbo_w;
    float fh = (float)(int)fbo_h;
    std::array<float, 16> exact_ortho = gl_utils::make_ortho(0.0f, fw, fh, 0.0f, -1.0f, 1.0f);
    glUniformMatrix4fv(m_uniform_ortho_quad, 1, GL_FALSE, exact_ortho.data());

    GLint quad_hdr_loc = glGetUniformLocation(m_quad_program, "u_use_hdr_display");
    glUniform1i(quad_hdr_loc, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fbo_texture_hdr[m_current_hdr_fbo]);

	render_color decay_color = { 1.0f, VECTOR_EFFECT_PHOSPHOR_DECAY, VECTOR_EFFECT_PHOSPHOR_DECAY, VECTOR_EFFECT_PHOSPHOR_DECAY };

    // Perfect quad covering from 0 to Width/Height without fractional decimals
    float fbo_verts[8] = { 0.0f, 0.0f, 0.0f, fh, fw, fh, fw, 0.0f };
    
    // --- UNIFIED UV MAPPING ---
    // Since our Ortho matrix sets (0,0) at the Top-Left for ALL FBOs and Screen,
    // we MUST always use standard flipped UVs to preserve visual orientation.
    float fbo_uv[8] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f };

    push_quad(fbo_verts, fbo_uv, decay_color);
    flush_batch();

    glUniform1i(quad_hdr_loc, m_use_hdr_display ? 1 : 0);

    m_current_texture = 0;
    m_last_blendmode = -1;

    m_current_hdr_fbo = next_hdr_fbo;
}


// Eliminates the massive CPU bottleneck caused by using std::sin() thousands of times per frame.
inline float fast_noise(float x, float y, float t) {
    int n = (int)(x * 10000.0f) + (int)(y * 30000.0f) + (int)(t * 10000.0f);
    n = (n << 13) ^ n;
    return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}

void gles3_renderer::apply_magnetic_jitter(float& px0, float& py0, float& px1, float& py1, bool is_vector, bool enable_advanced_effects, float current_time)
{
    // Early exit if the effect is disabled globally or not applicable
    if (!VECTOR_EFFECT_BEAM_JITTER_ENABLED || !is_vector || !enable_advanced_effects || VECTOR_EFFECT_BEAM_JITTER_AMOUNT <= 0.0f) {
        return;
    }

    float center_x = (px0 + px1) * 0.5f;
    float center_y = (py0 + py1) * 0.5f;

    // 1. RESOLUTION INDEPENDENCE (Normalize to 0.0 - 1.0)
    float nx = center_x / (float)std::max(m_width, 1);
    float ny = center_y / (float)std::max(m_height, 1);

    // 2. LOW-FREQUENCY DRIFT (Thermal and magnetic drift)
    float drift_x = std::sin(current_time * 2.1f + ny * 3.14f);
    float drift_y = std::cos(current_time * 1.8f + nx * 3.14f);

    // 3. AC MAINS HUM (Coil electromagnetic interference)
    float ac_x = std::sin(current_time * 45.0f + ny * 15.0f);
    float ac_y = std::cos(current_time * 55.0f + nx * 15.0f);

    // 4. THERMAL HASH (Approximate blue noise)
    // OPTIMIZATION Replaced the slow std::sin/cos with the Fast Noise hash
    float noise_x = fast_noise(nx, ny, current_time);
    float noise_y = fast_noise(ny, nx, current_time);

    // FINAL MIX: 40% Slow Drift | 40% AC Hum | 20% Thermal Noise
    float final_jx = (drift_x * 0.40f) + (ac_x * 0.40f) + (noise_x * 0.20f);
    float final_jy = (drift_y * 0.40f) + (ac_y * 0.40f) + (noise_y * 0.20f);

    // --- RESOLUTION SCALING (Strictly Linear) ---
    // To keep the visual amplitude identical across any screen density,
    // we scale the jitter directly relative to a 480p baseline.
    float scale_factor = (float)std::max(m_height, 1) / 480.0f;

    float jx = final_jx * VECTOR_EFFECT_BEAM_JITTER_AMOUNT * scale_factor;
    float jy = final_jy * VECTOR_EFFECT_BEAM_JITTER_AMOUNT * scale_factor;

    // Apply the offset to the vertices
    px0 += jx;
    py0 += jy;
    px1 += jx;
    py1 += jy;
}


void gles3_renderer::process_line_primitive(const local_primitive& prim, bool is_vector, bool enable_advanced_effects, float current_time, bool is_injected_corner)
{
    float px0 = prim.bounds.x0; float py0 = prim.bounds.y0;
    float px1 = prim.bounds.x1; float py1 = prim.bounds.y1;

    // --- MAGNETIC WOBBLE & THERMAL HASH (Analog Jitter) ---
    apply_magnetic_jitter(px0, py0, px1, py1, is_vector, enable_advanced_effects, current_time);

    // Calculate final line distances
    float dx = px1 - px0;
    float dy = py1 - py0;

	// Check if it's a zero-length primitive
    bool is_point = (std::abs(dx) < 0.001f && std::abs(dy) < 0.001f);
    
    // Distinguish between genuine game points (stars) and injected corner burns
    bool is_genuine_point = is_point && !is_injected_corner;
    
    float length = is_point ? 0.0f : std::sqrt(dx*dx + dy*dy);

	// 1. Get raw MAME intensity (VOLTAGE from 0.0 to 1.0)
    float voltage = prim.color.a;

	// --- VOLTAGE FLICKER (The ultimate anti-AA jitter) ---
    // High DPI screens hide positional jumps with anti-aliasing.
    // By injecting high-frequency noise directly into the brightness of the beam,
    // we guarantee the "electric buzz" is visible at ANY resolution.
    if (is_vector && enable_advanced_effects && VECTOR_EFFECT_BEAM_JITTER_ENABLED) {
        // OPTIMIZATION: Replaced the per-primitive std::sin with the Fast Noise hash
        float hash = fast_noise(px0, py0, current_time * 10.0f);

        // Flicker simulates a voltage drop, so it multiplies the voltage directly
        float flicker = 1.0f - (std::abs(hash) * VECTOR_EFFECT_BEAM_FLICKER_AMOUNT);
        voltage *= flicker;
    }

    float base_intensity = voltage;

    // --- CRT ELECTRON GUN PHYSICS (Voltage to Linear Light) ---
    if (enable_advanced_effects && VECTOR_EFFECT_LINEAR_GAMMA) {
        // Convert electrical voltage into physical light energy (photons)
        // using the characteristic Gamma curve of a CRT tube (~2.5).
        //base_intensity = std::pow(voltage, 2.5f);
		base_intensity = (voltage * voltage) * std::sqrt(voltage);
    }

	// --- PHOSPHOR COLOR RESPONSE (Luminance & Bleed) ---
    float phosphor_response = 1.0f;
    float drive = enable_advanced_effects ? VECTOR_EFFECT_GLOBAL_DRIVE_MULTIPLIER : 1.0f;

    if (enable_advanced_effects && VECTOR_EFFECT_PHOSPHOR_RESPONSE_ENABLED) {
        float luminance = (prim.color.r * VECTOR_EFFECT_PHOSPHOR_WEIGHT_R) + (prim.color.g * VECTOR_EFFECT_PHOSPHOR_WEIGHT_G) + (prim.color.b * VECTOR_EFFECT_PHOSPHOR_WEIGHT_B);
        phosphor_response = VECTOR_EFFECT_PHOSPHOR_BASE_RESPONSE + (luminance * VECTOR_EFFECT_PHOSPHOR_LUMA_BOOST);
    }

	// 2. Apply simulated energy physics
    float simulated_energy = base_intensity * drive * phosphor_response;
	
    // EXTRA ENERGY BOOST FOR POINTS/STARS
    if (enable_advanced_effects && is_genuine_point) {
        simulated_energy *= VECTOR_EFFECT_POINT_ENERGY_BOOST;
    }

	// 3. BEAM SPEED DYNAMICS (Electron gun physics)
    float dynamic_width_boost = 1.0f;
    if (enable_advanced_effects && is_vector && !is_point && VECTOR_EFFECT_BEAM_DYNAMICS_ENABLED) {
        
        static int last_w = 0, last_h = 0;
        static float cached_diagonal = 0.0f;
        
        if (m_width != last_w || m_height != last_h) {
            cached_diagonal = std::sqrt((m_width * m_width) + (m_height * m_height));
            last_w = m_width;
            last_h = m_height;
        }
        
        float threshold = cached_diagonal * VECTOR_EFFECT_SHORT_LINE_THRESHOLD_PCT; 
        if (length < threshold && length > 0.1f) {
            float shortness = 1.0f - (length / threshold);
            simulated_energy *= (1.0f + shortness * VECTOR_EFFECT_SHORT_LINE_INTENSITY_BOOST);
            dynamic_width_boost = (1.0f + shortness * VECTOR_EFFECT_SHORT_LINE_WIDTH_BOOST);
        }
    }

	// ====================================================================
    // 4 & 5. ROUTE ENERGY (HDR vs SDR)
    // ====================================================================
    float col_r, col_g, col_b;
    float core_alpha = 1.0f;
    // bloom is now handled optically by the Kawase FBO chain.

    if (enable_advanced_effects) {
        // --- TRUE HDR PATH (16-Bit Float FBO) ---
        // Pre-multiply intensity directly into RGB.
        // We inject raw, uncapped energy into the framebuffer so the 
        // Kawase bloom shaders can physically scatter the light.
        col_r = prim.color.r * simulated_energy;
        col_g = prim.color.g * simulated_energy;
        col_b = prim.color.b * simulated_energy;

        core_alpha = 1.0f; // Alpha acts strictly as a spatial mask (1.0)

	if (VECTOR_EFFECT_OVERBRIGHT_ENABLED) {
            float overbright_raw = std::max(simulated_energy - 1.0f, 0.0f);
            
            // CROSSTALK (Color Desaturation / Highlight Simulation)
            // If the light intensity exceeds 1.0, the excess energy "spills over"
            // into the other color channels, pushing the blinding core towards 
            // pure white before the FBO bloom scatters it.
            if (overbright_raw > 0.0f) {
                float crosstalk = overbright_raw * VECTOR_EFFECT_OVERBRIGHT_CROSSTALK; 
                col_r += crosstalk; col_g += crosstalk; col_b += crosstalk;
            }
        }
    } else {
        // --- NO ADVANCED PATH(8-Bit SDR Standard) ---
        // We CANNOT pre-multiply the alpha into RGB here because MAME's
        // 8-bit pipeline and classic glBlendFunc expect traditional RGBA.
        col_r = prim.color.r;
        col_g = prim.color.g;
        col_b = prim.color.b;

        // In 8-bit, Alpha controls the intensity. We clamp it safely.
        core_alpha = base_intensity;
    }

	// --- DRAW LASER CORE ONLY (No geometric halos) ---
	float width_mult = 1.0f;
	if (enable_advanced_effects) {
	    width_mult = is_genuine_point ? VECTOR_EFFECT_CORE_POINT_WIDTH_MULT : VECTOR_EFFECT_CORE_LINE_WIDTH_MULT;
	}
    float ideal_width = std::max(prim.width * width_mult * dynamic_width_boost, 0.5f);
	// --- RESOLUTION ENERGY PRESERVATION ---
    // If FBO is half-res, 1.0 logical pixel = 0.5 physical FBO pixels.
    // We must guarantee at least 1.0 physical pixel to prevent OpenGL 
    // sub-pixel rasterization energy loss.
	float min_safe_w = 1.0f / s_active_fbo_scale;	
	float safe_core_w = std::max(ideal_width, min_safe_w);
    float comp_core = ideal_width / safe_core_w;

	// ====================================================================
    // 6. DRAW PRIMITIVES
    // ====================================================================

    if (is_point) {
        float safe_half_w = safe_core_w * 0.5f;
        m_quad_verts[0] = px0 - safe_half_w; m_quad_verts[1] = py0 - safe_half_w;
        m_quad_verts[2] = px0 - safe_half_w; m_quad_verts[3] = py0 + safe_half_w;
        m_quad_verts[4] = px0 + safe_half_w; m_quad_verts[5] = py0 + safe_half_w;
        m_quad_verts[6] = px0 + safe_half_w; m_quad_verts[7] = py0 - safe_half_w;
        
        render_color c_core = enable_advanced_effects ? 
            render_color{1.0f, col_r * comp_core, col_g * comp_core, col_b * comp_core} : // HDR Pre-multiplied
            render_color{core_alpha * comp_core, col_r, col_g, col_b}; // SDR Standard Alpha
            
        push_quad(m_quad_verts, nullptr, c_core);
    } else {
		auto [b0, b1] = render_line_to_quad(px0, py0, px1, py1, dx, dy, length, safe_core_w);
        //bool use_aa = PRIMFLAG_GET_ANTIALIAS(prim.flags);
		bool use_aa = PRIMFLAG_GET_ANTIALIAS(prim.flags) && !enable_advanced_effects;
        const line_aa_step* step = use_aa ? line_aa_4step : line_aa_1step;

        for (; step->weight != 0.0f; step++) {
            render_color c;

            // LINE CORE (Dynamic AA discrimination)
            if (enable_advanced_effects) {
                // --- HDR PATH: Pre-multiply AA weight into RGB ---
                c.a = 1.0f;
                c.r = col_r * step->weight * comp_core;
                c.g = col_g * step->weight * comp_core;
                c.b = col_b * step->weight * comp_core;
            } else {
                // --- SDR PATH: Apply AA weight to Alpha only ---
                c.a = core_alpha * step->weight * comp_core;
                c.r = col_r;
                c.g = col_g;
                c.b = col_b;
            }

            m_quad_verts[0] = b0.x0 + step->xoffs; m_quad_verts[1] = b0.y0 + step->yoffs;
            m_quad_verts[2] = b0.x1 + step->xoffs; m_quad_verts[3] = b0.y1 + step->yoffs;
            m_quad_verts[4] = b1.x1 + step->xoffs; m_quad_verts[5] = b1.y1 + step->yoffs;
            m_quad_verts[6] = b1.x0 + step->xoffs; m_quad_verts[7] = b1.y0 + step->yoffs;
            
            push_quad(m_quad_verts, nullptr, c);
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

            // --- FIX: SHADER SCALING ON DIFFERENT WINDOW SIZES ---
            // Pass the actual physical viewport dimensions (m_view_width, m_view_height) 
            // as the OutputSize to the shader, just like in the SDR path.
            // This ensures resolution-dependent effects (like CRT Mattias or Pixelate)
            // scale perfectly and don't appear distorted or disproportionately huge.
            m_filter.draw_quad(m_current_texture, m_quad_verts, m_quad_uv, prim.texture->texinfo.width, prim.texture->texinfo.height, m_view_width, m_view_height);

            glUseProgram(m_quad_program);
            m_current_texture = 0; 
            m_last_blendmode = -1;
        } else {
            push_quad(m_quad_verts, m_quad_uv, prim.color);
        }
    } else {
        push_quad(m_quad_verts, nullptr, prim.color);
    }
}

void gles3_renderer::render()
{
	double exact_time = (double)osd_ticks() / (double)osd_ticks_per_second();
	float current_time = (float)std::fmod(exact_time, 100.0);

	// =========================================================
	// READ OFF-SCREEN ENERGY AND RESET FOR NEXT MAME CPU FRAME
	// =========================================================
	float current_frame_glow = g_hack_offscreen_overdrive;
	g_hack_offscreen_overdrive = 0.0f; // Reset to 0 immediately
	
	// Check if the Master Advanced Switch is enabled in MAME's core
    bool enable_advanced_effects = myosd_get(MYOSD_VECTOR_IMPROVED) ? true : false;

	// Only apply the glow if both the master switch and the specific effect are enabled
	if (enable_advanced_effects && VECTOR_EFFECT_OFFSCREEN_GLOW_ENABLED) {
	    // Multiply by the user-configurable aesthetic factor (slider)
	    m_current_monitor_glow = current_frame_glow * VECTOR_EFFECT_OFFSCREEN_GLOW_MULT;
	} else {
	    m_current_monitor_glow = 0.0f;
	}

	// ELECTRON GUN STATE (To calculate Dwell Time at the corners) ---
    float prev_x = -9999.0f;
    float prev_y = -9999.0f;
    float prev_dx_norm = 0.0f;
    float prev_dy_norm = 0.0f;

	static std::vector<local_primitive> draw_prims;
    static std::vector<GLuint> delete_texs;

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
	
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	if (m_init) { m_init = false; }

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

	GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] > 0 && viewport[3] > 0)
    {
        // --- ROTATION & RESIZE DETECTION ---
        // Force FBOs to be rebuilt if the physical viewport dimensions change 
        // (e.g., device rotation or window resize) to prevent "cut off" screen artifacts.
        if (m_view_width != viewport[2] || m_view_height != viewport[3]) {
            m_fbo_dirty = true;
        }
        m_view_width = viewport[2];
        m_view_height = viewport[3];
    }

	upload_pending_textures(draw_prims);

	// ------------------------------------------------------------------
    // PRE-PASS: Fast check for vectors & SCENE ENERGY RADAR (Auto-Exposure)
    // ------------------------------------------------------------------
    bool has_vectors = calculate_auto_exposure(draw_prims);

	glUseProgram(m_quad_program);
	glUniformMatrix4fv(m_uniform_ortho_quad, 1, GL_FALSE, m_ortho.data());
	
    // --- CACHED UNIFORMS & FAKE HDR ---
	glUniform1i(m_loc_quad_use_hdr, m_use_hdr_display ? 1 : 0);	
    
	// Auto HDR MUST be disabled for backdrops/bezels in Vector games
    glUniform1i(m_loc_quad_raster_fake_hdr, (HDR_RASTER_FAKE_HDR_ENABLED && !has_vectors) ? 1 : 0);
    
	glUniform1f(m_loc_quad_raster_hdr_mult, HDR_RASTER_HDR_MULTIPLIER);
    glUniform1f(m_loc_quad_paper_white, HDR_RASTER_PAPER_WHITE);
	glUniform1f(m_loc_quad_device_peak, m_peak_nits);	

	m_current_texture = 0;
    m_last_blendmode = -1;

	int vectorbuf_count = 0;
    for (const auto& prim : draw_prims) {
        if (PRIMFLAG_GET_VECTORBUF(prim.flags)) vectorbuf_count++;
    }
    // Disable persistence entirely if multiple monitors (VECTORBUFs) are detected
    // to avoid complex FBO state entanglement.
    m_multi_monitor_detected = (vectorbuf_count > 1);
    
    // If the fallback is active, we unconditionally disable the HDR routing
    bool require_hdr = has_vectors && enable_advanced_effects && !m_hdr_fallback_active;
    
    // Condition to check if intermediate filtering FBO is needed
    bool require_filter_fbo = m_use_hdr_display && m_usefilter;

    // If the GPU downgraded to SDR, we ensure require_sdr is TRUE to apply 
    // at least the basic smoothing filter and not lose the vector graphics.
    // If require_filter_fbo is TRUE, we must allocate the SDR buffer to catch the standard raster quads.
    bool require_sdr = (has_vectors && (m_usefilter || m_hdr_fallback_active)) || require_filter_fbo;

    // FBO state tracking
    static float last_fbo_w = 0.0f;
    static float last_fbo_h = 0.0f;
    bool fbo_initialized = false;

    render_bounds layout_bounds = { 0.0f, 0.0f, (float)m_width, (float)m_height };
    float layout_w = (float)m_width;
    float layout_h = (float)m_height;
    float fbo_w = layout_w;
    float fbo_h = layout_h;

    // Create a specialized ortho matrix for the vector pass.
    // This forces OpenGL to stretch and fit coordinates inside the smaller FBO seamlessly.
    auto vector_ortho = gl_utils::make_ortho(layout_bounds.x0, layout_bounds.x1, layout_bounds.y1, layout_bounds.y0, -1.0f, 1.0f);

    // If no vectors are active but we need an HDR filter pass, MAME wont submit a VECTORBUF primitive.
    // We explicitly allocate and clear our drawing targets here using the global dimensions.
    if (!has_vectors && require_filter_fbo) {
        if (fbo_w != last_fbo_w || fbo_h != last_fbo_h || m_fbo_dirty) {
            delete_fbos();
            m_fbo_dirty = false;
            last_fbo_w = fbo_w;
            last_fbo_h = fbo_h;
        }
        create_fbos((int)fbo_w, (int)fbo_h, false, true, true);
        
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_sdr);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f); glClear(GL_COLOR_BUFFER_BIT);
        
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_filter);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f); glClear(GL_COLOR_BUFFER_BIT);
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        fbo_initialized = true;
    }

    // Always start targeted at the Screen (0) for background artworks
    int current_fbo = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_view_width, m_view_height);
		
	static osd_ticks_t last_layer_log_time = 0;
    osd_ticks_t current_ticks = osd_ticks();
    bool should_log_layers = false;	
	
	if (current_ticks - last_layer_log_time >= osd_ticks_per_second()) {
		#if 0
        should_log_layers = true;
		ANDROID_LOG("=== MAME Z-ORDER LAYER TRACE (Start) ===");
		#endif
        last_layer_log_time = current_ticks;
    }

    int line_counter = 0; 
	
    for (const local_primitive& prim : draw_prims)
    {
		if (should_log_layers) {
            if (prim.type == render_primitive::LINE) {
                line_counter++; 
            } else if (prim.type == render_primitive::QUAD) {
                if (line_counter > 0) {
                    ANDROID_LOG("   [-> %d VECTOR LINES DRAWN HERE <-]", line_counter);
                    line_counter = 0;
                }
                
                bool log_is_vector = PRIMFLAG_GET_VECTOR(prim.flags);
                bool log_is_vectorbuf = PRIMFLAG_GET_VECTORBUF(prim.flags);
                int log_blend_mode = PRIMFLAG_GET_BLENDMODE(prim.flags);
                
                std::string blend_str;
                switch(log_blend_mode) {
                    case BLENDMODE_NONE:         blend_str = "NONE"; break;
                    case BLENDMODE_ALPHA:        blend_str = "ALPHA"; break;
                    case BLENDMODE_RGB_MULTIPLY: blend_str = "MULTIPLY"; break;
                    case BLENDMODE_ADD:          blend_str = "ADD"; break;
                    default:                     blend_str = "UNKNOWN"; break;
                }

                ANDROID_LOG(" - QUAD | Blend: %-8s | isVector: %d | isVecBuf: %d | Tex: %s | Rect: [%.0f, %.0f, %.0f, %.0f]",
                            blend_str.c_str(), log_is_vector, log_is_vectorbuf, 
                            (prim.texture ? "YES" : "NO "),
                            prim.bounds.x0, prim.bounds.y0, prim.bounds.x1, prim.bounds.y1);
            }
        }
        // ------------------------------------------------------------------
				
        // ==================================================================
        // MULTI-MONITOR / COCKTAIL HANDLING (VECTORBUF)
        // ==================================================================
        // MAME sends a VECTORBUF primitive (a black quad) to mark the
        // beginning of a new vector screen layout.
        if (PRIMFLAG_GET_VECTORBUF(prim.flags)) {

            // 1. Flush any pending vectors from the PREVIOUS monitor to the screen
            if (fbo_initialized) {
                switch_fbo_target(0, current_fbo, require_sdr, fbo_w, fbo_h, layout_bounds, vector_ortho, has_vectors);
            }

            // 2. Capture the exact layout boundaries of the NEW monitor
            layout_bounds = prim.bounds;
			layout_w = std::max(layout_bounds.x1 - layout_bounds.x0, 1.0f);
            layout_h = std::max(layout_bounds.y1 - layout_bounds.y0, 1.0f);
            if (layout_w <= 0.0f) layout_w = (float)m_width;
            if (layout_h <= 0.0f) layout_h = (float)m_height;

			// --- DOWN-SAMPLING (FILLRATE OPTIMIZATION) ---
            fbo_w = layout_w;
            fbo_h = layout_h;
            if (fbo_h * VECTOR_EFFECT_FBO_SCALE >= VECTOR_EFFECT_FBO_MIN_HEIGHT) {
                fbo_w *= VECTOR_EFFECT_FBO_SCALE;
                fbo_h *= VECTOR_EFFECT_FBO_SCALE;
            } else if (fbo_h > VECTOR_EFFECT_FBO_MIN_HEIGHT) {
                float ratio = VECTOR_EFFECT_FBO_MIN_HEIGHT / fbo_h;
                fbo_w *= ratio;
                fbo_h *= ratio;
            }
			
			s_active_fbo_scale = fbo_h / layout_h;
			
            // ---------------------------------------------

            // 3. Reallocate FBO memory ONLY if the layout dimensions changed			
            if (fbo_w != last_fbo_w || fbo_h != last_fbo_h || m_fbo_dirty) {
                delete_fbos();
                m_fbo_dirty = false;
                last_fbo_w = fbo_w;
                last_fbo_h = fbo_h;
            }

            // 4. Create FBOs (if they were deleted or didn't exist yet)
            create_fbos((int)fbo_w, (int)fbo_h, require_hdr, require_sdr, require_filter_fbo);

			// 5. Clear ACTIVE FBOs or Apply Phosphor Persistence
			if (require_hdr) {
                bool apply_persistence = VECTOR_EFFECT_PHOSPHOR_PERSISTENCE_ENABLED && !m_multi_monitor_detected && m_history_valid;

                if (apply_persistence) {

					apply_phosphor_persistence(fbo_w, fbo_h);

                } else {
                    // Standard clear if history is invalid or persistence is disabled
                    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_hdr[m_current_hdr_fbo]);
                    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);
                    m_history_valid = true; // History will be valid for the next frame
                }
            }

            if (require_sdr) {
                glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_sdr);
				float alpha_clear = require_filter_fbo ? 0.0f : 1.0f;
                glClearColor(0.0f, 0.0f, 0.0f, alpha_clear);
				glClear(GL_COLOR_BUFFER_BIT);
            }
            if (require_filter_fbo) {
                glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_filter);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f); glClear(GL_COLOR_BUFFER_BIT);
            }

            // Return binding to Screen (0) to maintain state consistency
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            // 6. Generate the specific ortho matrix for this layout
            vector_ortho = gl_utils::make_ortho(layout_bounds.x0, layout_bounds.x1, layout_bounds.y1, layout_bounds.y0, -1.0f, 1.0f);

            fbo_initialized = true;

            // Skip drawing this black quad (our FBOs are already cleared)
			//continue;
        }

        bool is_screen = PRIMFLAG_GET_SCREENTEX(prim.flags);
        bool is_vector = PRIMFLAG_GET_VECTOR(prim.flags);

        // Target: Vectors go to FBOs. UI, Backgrounds, and Raster games stay on Screen (0).
        int target_fbo = 0;
        if (is_vector) {
            if (require_hdr) target_fbo = 2;
            else if (require_sdr) target_fbo = 1;
        } else if (is_screen && require_filter_fbo) {
            // Route the raw low-res game screen directly to the Filter FBO
            // so pixel-art shaders can detect the native resolution properly.
            target_fbo = 3; 
        }

        // --- PIPELINE FLUSH ---
        // Context switcher automatically flushes and binds correct shaders/matrices
        switch_fbo_target(target_fbo, current_fbo, require_sdr, fbo_w, fbo_h, layout_bounds, vector_ortho, has_vectors);

		GLuint needed_tex = (prim.texture != nullptr) ? prim.texture->texture_id : m_white_texture;
		
		
        int needed_blend = PRIMFLAG_GET_BLENDMODE(prim.flags);
		int needed_is_vector = is_vector ? 1 : 0;

		if (m_current_texture != needed_tex || m_last_blendmode != needed_blend || m_last_is_vector != needed_is_vector) {
            flush_batch();
            m_current_texture = needed_tex; set_blendmode(needed_blend);
			
            m_last_is_vector = needed_is_vector;
            
            // Upload the Linear vs sRGB Gamma flag to the active shader
            glUseProgram(m_quad_program);
            glUniform1i(m_uniform_is_vector_quad, m_last_is_vector);
            					
			if (needed_blend == BLENDMODE_RGB_MULTIPLY) {
                // 1. OVERLAYS (Transparent cellophane/gels)
                glUniform1f(m_loc_quad_paper_white, 80.0f);
                glUniform1i(m_loc_quad_raster_fake_hdr, 0); 
            } else if (has_vectors && prim.is_artwork) {
                // 2. ARTWORKS IN VECTOR GAMES (Cardboard backgrounds, Bezels, Menus)
                // THE LIGHTING TRICK: If you press TAB, the light turns on fully (standard Paper White).
                // If the menu is closed, apply the darkness from the user's toggle.
				float current_nits = (m_in_menu || !HDR_DIM_VECTOR_ARTWORKS) ? HDR_RASTER_PAPER_WHITE : 85.0f;
                glUniform1f(m_loc_quad_paper_white, current_nits); 
                glUniform1i(m_loc_quad_raster_fake_hdr, 0); 
            } else {
                // 3. STANDARD RASTER GAMES
                glUniform1f(m_loc_quad_paper_white, HDR_RASTER_PAPER_WHITE);
                glUniform1i(m_loc_quad_raster_fake_hdr, (HDR_RASTER_FAKE_HDR_ENABLED && !has_vectors) ? 1 : 0);
            }

            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_current_texture);
        }
        switch (prim.type)
        {
            case render_primitive::LINE:
                process_dwell_point(prim, is_vector, enable_advanced_effects, current_time, prev_x, prev_y, prev_dx_norm, prev_dy_norm);
                process_line_primitive(prim, is_vector, enable_advanced_effects, current_time);
                break;
            case render_primitive::QUAD:
                process_quad_primitive(prim, is_screen, needed_blend);
                break;
            case render_primitive::INVALID: break;
        }
    }
	
	if (should_log_layers) {
        if (line_counter > 0) {
             ANDROID_LOG("   [-> %d VECTOR LINES DRAWN HERE <-]", line_counter);
        }
        ANDROID_LOG("=== MAME Z-ORDER LAYER TRACE (End) ===");
    }

    flush_batch();

	// --- TRAILING PIPELINE FLUSH ---
    // Force context switch back to 0 to trigger resolve/filter cascading logic for the LAST monitor processed
    if (fbo_initialized) {
        switch_fbo_target(0, current_fbo, require_sdr, fbo_w, fbo_h, layout_bounds, vector_ortho, has_vectors);
    }

    if (!delete_texs.empty()){
        glDeleteTextures(delete_texs.size(), delete_texs.data());
	}
	
	glBindTexture(GL_TEXTURE_2D, 0);
    m_current_texture = 0;
	m_last_is_vector = -1;
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

	// clean old textures
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

// Helper to map and log analog slider values
static float map_slider(const std::string& key, int slider_val, float min_val, float max_val) {
    int clamped_val = std::max(0, std::min(100, slider_val));
    float mapped_val = min_val + ((float)clamped_val / 100.0f) * (max_val - min_val);
    
    // Log both the raw slider value (0-100) and the final physical mapped value
    ANDROID_LOG("Renderer Param: %s | Raw Slider: %d -> Mapped Physics: %.4f", key.c_str(), slider_val, mapped_val);
    
    return mapped_val;
}

// Helper to parse and log boolean switches
static bool parse_bool(const std::string& key, const std::string& val) {
    bool result = (val == "1" || val == "true");
    
    // Log the raw incoming string and its boolean resolution
    ANDROID_LOG("Renderer Param: %s | Raw String: %s -> Mapped Bool: %s", key.c_str(), val.c_str(), result ? "TRUE" : "FALSE");
    
    return result;
}

// =======================================================================
// JNI BRIDGE EXPORTS (Receiving vector parameters)
// =======================================================================
extern "C" {

    void gles3_renderer_setParameters(const char** keys, const char** values, int count) 
    {
        ANDROID_LOG("=== gles3_renderer_setParameters received with %d parameters ===", count);

        for(int i = 0; i < count; i++) {
            
            std::string key = keys[i];
            std::string val = values[i];

// --- 0. FBO PERFORMANCE ---
            if (key == "PREF_VECTOR_EFFECT_FBO_HALF_RES") {
                bool new_half_res = parse_bool(key, val);
                
                if (new_half_res != VECTOR_FBO_HALF_RES) {
                    VECTOR_FBO_HALF_RES = new_half_res;
                    VECTOR_EFFECT_FBO_SCALE = VECTOR_FBO_HALF_RES ? 0.5f : 1.0f;                  
                    if (g_current_renderer != nullptr) {
                        g_current_renderer->set_fbo_dirty();
                    }
                }
            }

            // --- 1. OPTICAL BLOOM (HALO SCATTERING) ---
            else if (key == "PREF_VECTOR_EFFECT_BLOOM") VECTOR_EFFECT_BLOOM_ENABLED = parse_bool(key, val);
            else if (key == "PREF_BLOOM_KAWASE_PASSES") {
                int new_passes = std::max(1, std::min(6, std::stoi(val)));
                if (new_passes != BLOOM_KAWASE_PASSES) {
                    BLOOM_KAWASE_PASSES = new_passes;
                    if (g_current_renderer != nullptr) {
                        g_current_renderer->set_fbo_dirty();
                    }
                }
            }
            else if (key == "PREF_BLOOM_KAWASE_RADIUS") BLOOM_KAWASE_RADIUS = map_slider(key, std::stoi(val), 0.0f, 2.50f);
            else if (key == "PREF_BLOOM_KAWASE_THRESHOLD") BLOOM_KAWASE_THRESHOLD = map_slider(key, std::stoi(val), 0.0f, 2.00f);
            else if (key == "PREF_BLOOM_KAWASE_INTENSITY") BLOOM_KAWASE_INTENSITY = map_slider(key, std::stoi(val), 0.0f, 0.50f);

            // --- 1B. VECTOR CORE GEOMETRY ---
            else if (key == "PREF_VECTOR_EFFECT_CORE_LINE_WIDTH") VECTOR_EFFECT_CORE_LINE_WIDTH_MULT = map_slider(key, std::stoi(val), 0.0f, 2.50f);
            else if (key == "PREF_VECTOR_EFFECT_CORE_POINT_WIDTH") VECTOR_EFFECT_CORE_POINT_WIDTH_MULT = map_slider(key, std::stoi(val), 0.0f, 5.00f);
            else if (key == "PREF_VECTOR_EFFECT_POINT_ENERGY_BOOST") VECTOR_EFFECT_POINT_ENERGY_BOOST = map_slider(key, std::stoi(val), 1.0f, 6.00f);
			
            // --- 2. GLOBAL EXPOSURE & CRT DRIVE ---
            else if (key == "PREF_VECTOR_EFFECT_AUTO_EXPOSURE") VECTOR_EFFECT_AUTO_EXPOSURE_ENABLED = parse_bool(key, val);
            else if (key == "PREF_VECTOR_EFFECT_FIXED_EXPOSURE") VECTOR_EFFECT_FIXED_EXPOSURE = map_slider(key, std::stoi(val), 0.5f, 2.5f);
            else if (key == "PREF_VECTOR_EFFECT_GLOBAL_DRIVE") VECTOR_EFFECT_GLOBAL_DRIVE_MULTIPLIER = map_slider(key, std::stoi(val), 1.0f, 3.0f);
            else if (key == "PREF_VECTOR_EFFECT_BASE_NITS") VECTOR_EFFECT_BASE_NITS = map_slider(key, std::stoi(val), 100.0f, 1100.0f);
            else if (key == "PREF_VECTOR_EFFECT_MAX_NITS") VECTOR_EFFECT_MAX_NITS = map_slider(key, std::stoi(val), 100.0f, 1100.0f);            
            else if (key == "PREF_VECTOR_EFFECT_AUTO_EXPOSURE_MULT") VECTOR_EFFECT_AUTO_EXPOSURE_MULT = map_slider(key, std::stoi(val), 0.5f, 2.0f);
            else if (key == "PREF_VECTOR_EFFECT_AUTO_EXPOSURE_THRESHOLD") VECTOR_EFFECT_AUTO_EXPOSURE_THRESHOLD = map_slider(key, std::stoi(val), 0.0f, 0.10f);

            // --- 3. EXCESS ENERGY (HDR OVERBRIGHT) ---
            else if (key == "PREF_VECTOR_EFFECT_OVERBRIGHT") VECTOR_EFFECT_OVERBRIGHT_ENABLED = parse_bool(key, val);
            else if (key == "PREF_VECTOR_EFFECT_OVERBRIGHT_MAX") VECTOR_EFFECT_OVERBRIGHT_MAX = map_slider(key, std::stoi(val), 1.0f, 5.0f);
            else if (key == "PREF_VECTOR_EFFECT_OVERBRIGHT_LINE_MULT") VECTOR_EFFECT_OVERBRIGHT_LINE_MULT = map_slider(key, std::stoi(val), 0.0f, 3.0f);
            else if (key == "PREF_VECTOR_EFFECT_OVERBRIGHT_POINT_MULT") VECTOR_EFFECT_OVERBRIGHT_POINT_MULT = map_slider(key, std::stoi(val), 0.0f, 5.0f);
            else if (key == "PREF_VECTOR_EFFECT_OVERBRIGHT_CROSSTALK") VECTOR_EFFECT_OVERBRIGHT_CROSSTALK = map_slider(key, std::stoi(val), 0.0f, 1.0f);

            // --- 4. ELECTRON BEAM PHYSICS ---
            else if (key == "PREF_VECTOR_EFFECT_BEAM_DYNAMICS") VECTOR_EFFECT_BEAM_DYNAMICS_ENABLED = parse_bool(key, val);
            else if (key == "PREF_VECTOR_EFFECT_SHORT_LINE_INTENSITY") VECTOR_EFFECT_SHORT_LINE_INTENSITY_BOOST = map_slider(key, std::stoi(val), 0.0f, 2.0f);
            else if (key == "PREF_VECTOR_EFFECT_SHORT_LINE_WIDTH") VECTOR_EFFECT_SHORT_LINE_WIDTH_BOOST = map_slider(key, std::stoi(val), 0.0f, 1.0f);
            
            else if (key == "PREF_VECTOR_EFFECT_CORNER_BURN") VECTOR_EFFECT_CORNER_BURN_ENABLED = parse_bool(key, val);
            else if (key == "PREF_VECTOR_EFFECT_CORNER_DOT_THRESHOLD") VECTOR_EFFECT_CORNER_DOT_THRESHOLD = map_slider(key, std::stoi(val), 0.0f, 1.0f);
            else if (key == "PREF_VECTOR_EFFECT_CORNER_BURN_BOOST") VECTOR_EFFECT_CORNER_BURN_BOOST = map_slider(key, std::stoi(val), 1.0f, 3.0f);
            else if (key == "PREF_VECTOR_EFFECT_CORNER_BURN_WIDTH_MULT") VECTOR_EFFECT_CORNER_BURN_WIDTH_MULT = map_slider(key, std::stoi(val), 0.0f, 2.0f);

            // --- 5. PHOSPHOR CHEMISTRY (TRAILS & LUMINANCE) ---
            else if (key == "PREF_VECTOR_EFFECT_PHOSPHOR_RESPONSE") VECTOR_EFFECT_PHOSPHOR_RESPONSE_ENABLED = parse_bool(key, val);
            else if (key == "PREF_VECTOR_EFFECT_PHOSPHOR_BASE_RESPONSE") VECTOR_EFFECT_PHOSPHOR_BASE_RESPONSE = map_slider(key, std::stoi(val), 0.0f, 1.0f);
            else if (key == "PREF_VECTOR_EFFECT_PHOSPHOR_LUMA_BOOST") VECTOR_EFFECT_PHOSPHOR_LUMA_BOOST = map_slider(key, std::stoi(val), 0.0f, 1.0f);
            else if (key == "PREF_VECTOR_EFFECT_PERSISTENCE") {
                bool new_persistence = parse_bool(key, val);
                if (new_persistence != VECTOR_EFFECT_PHOSPHOR_PERSISTENCE_ENABLED) {
                    VECTOR_EFFECT_PHOSPHOR_PERSISTENCE_ENABLED = new_persistence;
                    // Toggling persistence changes the required number of HDR FBOs (1 vs 2).
                    // We must flag the FBOs as dirty to force an immediate safe unwinding and 
                    // reallocation, preventing Dangling Index crashes on the GPU.
                    if (g_current_renderer != nullptr) {
                        g_current_renderer->set_fbo_dirty();
                    }
                }
            }
            else if (key == "PREF_VECTOR_EFFECT_PHOSPHOR_DECAY") VECTOR_EFFECT_PHOSPHOR_DECAY = map_slider(key, std::stoi(val), 0.0f, 1.0f);

            // --- 6. ANALOG WEAR & TEAR (JITTER) ---
            else if (key == "PREF_VECTOR_EFFECT_JITTER") VECTOR_EFFECT_BEAM_JITTER_ENABLED = parse_bool(key, val);
            else if (key == "PREF_VECTOR_EFFECT_BEAM_JITTER_AMOUNT") VECTOR_EFFECT_BEAM_JITTER_AMOUNT = map_slider(key, std::stoi(val), 0.0f, 2.50f);
            else if (key == "PREF_VECTOR_EFFECT_BEAM_FLICKER_AMOUNT") VECTOR_EFFECT_BEAM_FLICKER_AMOUNT = map_slider(key, std::stoi(val), 0.0f, 0.50f);
			
			else if (key == "PREF_VECTOR_EFFECT_LINEAR_GAMMA") VECTOR_EFFECT_LINEAR_GAMMA = parse_bool(key, val);
			
			// --- 7. RASTER FAKE HDR ---
			else if (key == "PREF_HDR_RASTER_FAKE_HDR") HDR_RASTER_FAKE_HDR_ENABLED = parse_bool(key, val);
			else if (key == "PREF_HDR_DIM_VECTOR_ARTWORKS") HDR_DIM_VECTOR_ARTWORKS = parse_bool(key, val);			
            else if (key == "PREF_HDR_RASTER_HDR_MULTIPLIER") HDR_RASTER_HDR_MULTIPLIER = map_slider(key, std::stoi(val), 1.0f, 5.0f);
            else if (key == "PREF_HDR_RASTER_PAPER_WHITE") HDR_RASTER_PAPER_WHITE = map_slider(key, std::stoi(val), 100.0f, 500.0f);

			// --- 8. OFF-SCREEN MONITOR GLOW ---
            else if (key == "PREF_VECTOR_EFFECT_OFFSCREEN_GLOW") VECTOR_EFFECT_OFFSCREEN_GLOW_ENABLED = parse_bool(key, val);
            else if (key == "PREF_VECTOR_EFFECT_OFFSCREEN_GLOW_MULT") VECTOR_EFFECT_OFFSCREEN_GLOW_MULT = map_slider(key, std::stoi(val), 0.0f, 1.0f);			

        }
    }

}