// license:BSD-3-Clause
// copyright-holders:David Valdeita (Seleuco) & Filipe Paulino (FlykeSpice)
/***************************************************************************

    shader_sources.hxx

    Shader sources for GLES 3.x renderer

***************************************************************************/

/* ========================================================================================
 * BASE QUAD VERTEX SHADER
 * ========================================================================================
 * Standard orthographic projection shader for 2D primitives.
 * Decodes packed vertex data (positions, UVs, color) based on the corner index.
 * Maps normalized device coordinates via the u_ortho projection matrix.
 * ======================================================================================== */

static const char* quad_vertex_shader_src = 
    "precision highp float;\n"
    "in float a_corner;\n"
    "in vec4 i_p0p1;\n"
    "in vec4 i_p2p3;\n"
    "in vec4 i_uv0uv1;\n"
    "in vec4 i_uv2uv3;\n"
    "in vec4 i_color;\n"
    "out vec2 v_texuv;\n"
    "out vec4 v_color;\n"
    "uniform mat4 u_ortho;\n"
    "void main() {\n"
    "    vec2 pos;\n"
    "    vec2 uv;\n"
    "    int corner = int(a_corner);\n"
    "    if (corner == 0)      { pos = i_p0p1.xy; uv = i_uv0uv1.xy; }\n"
    "    else if (corner == 1) { pos = i_p0p1.zw; uv = i_uv0uv1.zw; }\n"
    "    else if (corner == 2) { pos = i_p2p3.xy; uv = i_uv2uv3.xy; }\n"
    "    else                  { pos = i_p2p3.zw; uv = i_uv2uv3.zw; }\n"
    "    gl_Position = u_ortho * vec4(pos, 0.0, 1.0);\n"
    "    v_texuv = uv;\n"
    "    v_color = i_color;\n"
    "}\n";


/* ========================================================================================
 * PRIMITIVE FRAGMENT SHADER (TRUE HDR / AUTO-HDR)
 * ========================================================================================
 * Acts as the primary surface shader before post-processing. It branches based on 
 * the source material (vector lines vs. standard 8-bit raster graphics).
 * * Key paths:
 * - Vector Path: Passes mathematically linear, uncapped energy straight to the FBO.
 * - SDR / Raster Path: Converts standard sRGB textures to linear space.
 * - Fake HDR (Auto-HDR): Analyzes 8-bit games and applies Inverse Tone Mapping to 
 * expand bright pixels (explosions, skies) into the HDR luminance range, while 
 * keeping UI and standard elements strictly at the SDR 'Paper White' level.
 * - Hardware Mapping: Maps expanded luminance to the physical peak nits of the 
 * current device display using piecewise Reinhard compression to prevent clipping.
 * ======================================================================================== */

static const char* quad_frag_shader_src = 
    "precision highp float;\n" 
    "in vec2 v_texuv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D s_texture;\n"
    "uniform int u_use_hdr_display;\n"
    "uniform int u_is_vector;\n"
    "uniform int u_raster_fake_hdr;\n"
    "uniform float u_raster_hdr_mult;\n"
    "uniform float u_paper_white;\n"
    "uniform float u_device_peak_nits;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 texColor = texture(s_texture, v_texuv);\n"
    "    if (u_use_hdr_display == 1) {\n"
    "        if (u_is_vector == 1) {\n"
    "            // TRUE LINEAR PATH: Vectors and glow are already mathematically linear and calibrated\n"
    "            fragColor = vec4(texColor.rgb * v_color.rgb, texColor.a * v_color.a);\n"
    "        } else {\n"
    "            // SRGB PATH: Convert standard 8-bit textures (artworks/raster games) to Linear\n"
    "            texColor.rgb = pow(texColor.rgb, vec3(2.2));\n"
    "            vec3 linear_vcolor = pow(v_color.rgb, vec3(2.2));\n"
    "            vec3 final_sdr = texColor.rgb * linear_vcolor;\n"
    "\n"
    "            // --- RASTER PAPER WHITE CALIBRATION ---\n"
    "            float paper_white_mult = u_paper_white / 80.0;\n"
    "\n"
    "            if (u_raster_fake_hdr == 1) {\n"
    "                // --- FAKE HDR UPGRADE (Inverse Tone Mapping) ---\n"
    "                float luma = dot(final_sdr, vec3(0.2126, 0.7152, 0.0722));\n"
    "                float hdr_weight = pow(smoothstep(0.25, 1.0, luma), 2.0);\n"
    "                float current_boost = mix(1.0, u_raster_hdr_mult, hdr_weight);\n"
    "                vec3 fake_hdr = final_sdr * current_boost;\n"
    "\n"
    "                // Dynamic HDR Desaturation (Highlight Roll-off)\n"
    "                float boosted_luma = dot(fake_hdr, vec3(0.2126, 0.7152, 0.0722));\n"
    "                vec3 white_mix = vec3(boosted_luma);\n"
    "                float desat_amount = clamp(hdr_weight * (current_boost - 1.0) * 0.10, 0.0, 1.0);\n"
    "                fake_hdr = mix(fake_hdr, white_mix, desat_amount);\n"
    "\n"
    "                // --- HARDWARE DYNAMIC TONE MAPPING (Piecewise Reinhard) ---\n"
    "                // Convert Android's dynamic display capabilities to scRGB limits.\n"
    "                float hardware_peak_scRGB = u_device_peak_nits / 80.0;\n"
    "                float max_nits_mult = max(hardware_peak_scRGB, paper_white_mult + 1.0);\n"
    "                vec3 raw_hdr_nits = fake_hdr * paper_white_mult;\n"
    "                float raw_hdr_luma = dot(raw_hdr_nits, vec3(0.2126, 0.7152, 0.0722));\n"
    "\n"
    "                // 1. Keep colors strictly linear up to Paper White (protects SDR pixel art)\n"
    "                float threshold = paper_white_mult;\n"
    "                float range = max_nits_mult - threshold;\n"
    "\n"
    "                // 2. Compress ONLY the excess HDR light into the remaining display headroom\n"
    "                float excess = max(raw_hdr_luma - threshold, 0.0);\n"
    "                float mapped_excess = (excess / (excess + range)) * range;\n"
    "                float mapped_luma = min(raw_hdr_luma, threshold) + mapped_excess;\n"
    "\n"
    "                // 3. Scale RGB proportionally by the compressed luminance to preserve hue and contrast\n"
    "                vec3 mapped_hdr = raw_hdr_nits * (mapped_luma / max(raw_hdr_luma, 0.0001));\n"
    "\n"
    "                fragColor = vec4(mapped_hdr, texColor.a * v_color.a);\n"
    "            } else {\n"
    "                // EXACT ORIGINAL HDR BEHAVIOR + PAPER WHITE\n"
    "                fragColor = vec4(final_sdr * paper_white_mult, texColor.a * v_color.a);\n"
    "            }\n"
    "        }\n"
    "    } else {\n"
    "        // CLASSIC SDR PATH\n"
    "        fragColor = texColor * v_color;\n"
    "    }\n"
    "}\n";
	
/* ========================================================================================
 * OPTICAL BLOOM: DOWNSAMPLE & CRT PHOSPHOR EXTRACTION (JIMENEZ 13-TAP) (DUAL KAWASE)
 * ========================================================================================
 * The first half of the Dual-Filter spatial convolution chain. Replaces standard Kawase 
 * with a 13-tap filter to eliminate temporal aliasing and macro-blocking on downsamples.
 * * CRT Phosphor Emulation Features:
 * - Dominant Color Extraction: Bypasses standard perceptual luma to ensure pure, highly 
 * saturated vector colors (e.g., pure P31 green in Star Wars or pure blue in Tempest) 
 * are not crushed by the extraction threshold.
 * - Anti-Popping Knee: Uses a raised knee threshold to ensure moving lines fade smoothly 
 * into the bloom rather than flickering on/off.
 * - Phosphor Saturation Curve: Applies a hybrid quadratic falloff (0.5x + 0.5x^2) to 
 * model how CRT glass diffuses light, retaining blinding core energy while gracefully 
 * rolling off mid-tones.
 * ======================================================================================== */
 
static const char* kawase_down_frag_shader_src = 
    "precision highp float;\n"
    "in vec2 v_texuv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D s_texture;\n"
    "uniform vec2 u_texel_size;\n"
    "uniform float u_threshold;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec2 uv = v_texuv;\n"
    "    vec2 texel = u_texel_size;\n"
    "\n"
    "    // 13-Tap Downsample Spatial Dispersion Filter\n"
    "    vec3 A = texture(s_texture, uv - texel).rgb;\n"
    "    vec3 B = texture(s_texture, uv + vec2(0.0, -texel.y)).rgb;\n"
    "    vec3 C = texture(s_texture, uv + vec2(texel.x, -texel.y)).rgb;\n"
    "    vec3 D = texture(s_texture, uv + vec2(-texel.x, 0.0)).rgb;\n"
    "    vec3 E = texture(s_texture, uv).rgb; // Center pixel\n"
    "    vec3 F = texture(s_texture, uv + vec2(texel.x, 0.0)).rgb;\n"
    "    vec3 G = texture(s_texture, uv + vec2(-texel.x, texel.y)).rgb;\n"
    "    vec3 H = texture(s_texture, uv + vec2(0.0, texel.y)).rgb;\n"
    "    vec3 I = texture(s_texture, uv + texel).rgb;\n"
    "\n"
    "    vec3 J = texture(s_texture, uv + vec2(-texel.x * 0.5, -texel.y * 0.5)).rgb;\n"
    "    vec3 K = texture(s_texture, uv + vec2( texel.x * 0.5, -texel.y * 0.5)).rgb;\n"
    "    vec3 L = texture(s_texture, uv + vec2(-texel.x * 0.5,  texel.y * 0.5)).rgb;\n"
    "    vec3 M = texture(s_texture, uv + vec2( texel.x * 0.5,  texel.y * 0.5)).rgb;\n"
    "\n"
    "    // CRT Core Weights (Sum = 0.94 for natural energy decay)\n"
    "    vec3 color = E * 0.16;\n"
    "    color += (A + C + G + I) * 0.04;\n"
    "    color += (B + D + F + H) * 0.07;\n"
    "    color += (J + K + L + M) * 0.085;\n"
    "\n"
    "    if (u_threshold > 0.0) {\n"
    "        // High-Persistence Phosphor Weights (Green-biased for Atari Vectors)\n"
    "        float perceptual_luma = dot(color, vec3(0.25, 0.70, 0.05));\n"
    "        float max_channel = max(color.r, max(color.g, color.b));\n"
    "        \n"
    "        // True Dominant Color Extraction\n"
    "        float avg_color = (color.r + color.g + color.b) / 3.0;\n"
    "        float dominant = max_channel - avg_color;\n"
    "        \n"
    "        // Gradual Extraction Ceiling (Fixed scaling)\n"
    "        // A pure color yields a dominant value of ~0.667. \n"
    "        // 0.667 * 0.55 = ~0.36, perfectly hitting the 0.35 ceiling without crushing mid-tones.\n"
    "        float factor = clamp(dominant * 0.55, 0.0, 0.35);\n"
    "        float luma = mix(perceptual_luma, max_channel, factor);\n"
    "        \n"
    "        // Anti-Popping Knee\n"
    "        float knee = max(0.04, u_threshold * 0.15);\n"
    "        \n"
    "        // Linear mapping -> Hybrid S-Curve for phosphor saturation\n"
    "        float linear_weight = clamp((luma - (u_threshold - knee)) / (2.0 * knee), 0.0, 1.0);\n"
    "        color *= linear_weight * (0.5 + 0.5 * linear_weight);\n"
    "    }\n"
    "    fragColor = vec4(color, 1.0);\n"
    "}\n";
	
/* ========================================================================================
 * OPTICAL BLOOM: UPSAMPLE & CRT ASTIGMATISM (9-TAP TENT)
 * ========================================================================================
 * The second half of the Dual-Filter chain. Uses a 9-tap tent filter to smoothly 
 * expand the downsampled light buffers back up without introducing grid artifacts.
 * * Includes an anisotropic scaling factor (e.g., stretching the X-axis) to emulate 
 * the imperfect magnetic deflection yoke of vintage CRT monitors, creating a classic 
 * horizontal optical flare.
 * ======================================================================================== */	
	
static const char* kawase_up_frag_shader_src = 
    "precision highp float;\n"
    "in vec2 v_texuv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D s_texture;\n"
    "uniform vec2 u_texel_size;\n"
    "uniform float u_radius;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec2 uv = v_texuv;\n"
    "    \n"
    "    // Anisotropy (CRT Astigmatism emulation).\n"
    "    // A real CRT yoke deflects electrons imperfectly, widening the beam horizontally.\n"
    "    // Multiplying x by 1.2 and y by 0.9 gives it that classic optical 'Star Wars' anamorphic flare.\n"
    "    vec2 texel = u_texel_size * vec2(u_radius * 1.2, u_radius * 0.9);\n"
    "\n"
    "    // 9-Tap Tent Upsample - Perfect mathematical smoothing without grid/block artifacts\n"
    "    vec3 color = texture(s_texture, uv).rgb * 4.0;\n"
    "    color += texture(s_texture, uv + vec2(-texel.x, 0.0)).rgb * 2.0;\n"
    "    color += texture(s_texture, uv + vec2( texel.x, 0.0)).rgb * 2.0;\n"
    "    color += texture(s_texture, uv + vec2(0.0,  texel.y)).rgb * 2.0;\n"
    "    color += texture(s_texture, uv + vec2(0.0, -texel.y)).rgb * 2.0;\n"
    "\n"
    "    color += texture(s_texture, uv + vec2(-texel.x, -texel.y)).rgb * 1.0;\n"
    "    color += texture(s_texture, uv + vec2( texel.x, -texel.y)).rgb * 1.0;\n"
    "    color += texture(s_texture, uv + vec2(-texel.x,  texel.y)).rgb * 1.0;\n"
    "    color += texture(s_texture, uv + vec2( texel.x,  texel.y)).rgb * 1.0;\n"
    "\n"
    "    // Divide by the sum of weights (4 + 8 + 4 = 16)\n"
    "    fragColor = vec4(color / 16.0, 1.0);\n"
    "}\n";
	
/* ========================================================================================
 * FINAL COMPOSITION & TONE MAPPING (HDR / SDR)
 * ========================================================================================
 * The final pass that merges the razor-sharp core vector layer with the accumulated 
 * optical bloom buffer.
 * * - HDR Display Path: Maps the raw injected energy directly to physical nits (scRGB space)
 * so the device's actual OLED/LCD backlight handles the light emission. Uses a "shoulder" 
 * (Piecewise Reinhard) to gently compress extreme highlights that exceed the display's 
 * maximum physical capabilities.
 * - SDR Display Path: Falls back to a standard photographic exposure formula (1.0 - exp(-E)) 
 * and 2.2 gamma correction for legacy 8-bit screens.
 * - Anti-Fattening Mask: Outputs an alpha mask to prevent the OS compositor from 
 * unnecessarily blending black pixels, saving GPU bandwidth.
 * ======================================================================================== */	

static const char* hdr_frag_shader_src = 
    "precision highp float;\n"
    "in vec2 v_texuv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D s_texture;\n"
    "uniform sampler2D s_bloom;\n"
    "uniform float u_bloom_intensity;\n"
    "uniform float u_exposure;\n"
    "uniform int u_use_hdr_display;\n"
    "\n"
    "// --- DYNAMIC PARAMETERS (In Physical Nits) ---\n"
    "uniform float u_base_nits;         // Target nits for standard 1.0 intensity (e.g., 300.0)\n"
    "uniform float u_max_nits;          // Physical limit of the CRT phosphor (e.g., 400.0)\n"
    "uniform float u_device_peak_nits;  // Monitor max physical capability\n"
    "\n"
    "// --- OFF-SCREEN MONITOR GLOW ---\n"
    "uniform float u_offscreen_glow;    // Global energy accumulated from off-screen vectors\n"
    "\n"
    "out vec4 fragColor;\n"
    "\n"
    "void main() {\n"
    "    // 1. OFF-SCREEN TUBE GLOW CALCULATION\n"
    "    // Center is (0.5, 0.5). Max distance to corners is approx 0.707. Multiply by 1.414 to normalize to 1.0.\n"
    "    float dist = clamp(length(v_texuv - vec2(0.5, 0.5)) * 1.41421356, 0.0, 1.0);\n"
    "    // 1.0 brightness at center, falling off to 0.2 at the edges\n"
    "    float monitor_glow_shape = mix(1.0, 0.2, dist);\n"
    "    // Vintage CRT phosphor tint (slightly blue/cyan/white) for the ambient tube glow\n"
    "    vec3 glow_tint = vec3(0.85, 0.95, 1.0);\n"
    "    vec3 offscreen_ambient_light = glow_tint * u_offscreen_glow * monitor_glow_shape;\n"
    "\n"
    "    // 2. BEAM ENERGY FETCH (Core + Optical Bloom + Ambient Tube Glow)\n"
    "    vec3 core_beam = texture(s_texture, v_texuv).rgb * v_color.rgb;\n"
    "    vec3 bloom_halo = texture(s_bloom, v_texuv).rgb * u_bloom_intensity;\n"
    "    vec3 beam = core_beam + bloom_halo + offscreen_ambient_light;\n"
    "\n"
    "    vec3 mapped;\n"
    "    float out_mask = 0.0;\n"
    "\n"
    "    if (u_use_hdr_display == 1) {\n"
    "        // --- HDR PATH (scRGB Linear Space) ---\n"
    "        float hardware_peak_scRGB = u_device_peak_nits / 80.0;\n"
    "        float max_nits_mult = min(u_max_nits / 80.0, hardware_peak_scRGB);\n"
    "        float base_mult = u_base_nits / 80.0;\n"
    "\n"
    "        max_nits_mult = max(max_nits_mult, base_mult + 1.0);\n"
    "\n"
    "        // LINEAR SCALING\n"
    "        vec3 raw_hdr_nits = beam * u_exposure * base_mult;\n"
    "        float raw_hdr_luma = dot(raw_hdr_nits, vec3(0.2126, 0.7152, 0.0722));\n"
    "\n"
    "        // HIGHLIGHT COMPRESSION THRESHOLD (The Shoulder)\n"
    "        float threshold = base_mult * 0.75;\n"
    "\n"
    "        // ANTI-FATTENING MASK\n"
    "        out_mask = smoothstep(threshold, threshold * 2.0, raw_hdr_luma) * 0.90;\n"
    "\n"
    "        // HIGHLIGHT COMPRESSION (Piecewise Reinhard)\n"
    "        float range = max_nits_mult - threshold;\n"
    "        float excess = max(raw_hdr_luma - threshold, 0.0);\n"
    "        float mapped_excess = (excess / (excess + range)) * range;\n"
    "        float mapped_luma = min(raw_hdr_luma, threshold) + mapped_excess;\n"
    "\n"
    "        // SCALE RGB PROPORTIONALLY (Preserve Hue & Saturation)\n"
    "        mapped = raw_hdr_nits * (mapped_luma / max(raw_hdr_luma, 0.0001));\n"
    "\n"
    "    } else {\n"
    "        // --- SDR PATH (8-bit Standard Displays) ---\n"
    "        vec3 raw = vec3(1.0) - exp(-beam * u_exposure);\n"
    "\n"
    "        // DISPLAY GAMMA CORRECTION (OETF)\n"
    "        mapped = pow(clamp(raw, 0.0, 1.0), vec3(1.0 / 2.2));\n"
    "\n"
    "        float sdr_luma = dot(raw, vec3(0.2126, 0.7152, 0.0722));\n"
    "        out_mask = smoothstep(0.1, 0.8, sdr_luma) * 0.90;\n"
    "    }\n"
    "\n"
    "    // --- SAFEGUARD THE COMPOSITOR MASK ---\n"
    "    // Make sure the glow actually overrides the alpha mask so the OS \n"
    "    // doesn't blend the Android desktop through our flash effect.\n"
    "    out_mask = max(out_mask, clamp(u_offscreen_glow * monitor_glow_shape, 0.0, 1.0));\n"
    "\n"
    "    // Output the mapped color and the carving mask in the alpha channel\n"
    "    fragColor = vec4(mapped, out_mask);\n"
    "}\n";