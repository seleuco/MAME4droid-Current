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
    "    vec2 pos_arr[4];\n"
    "    pos_arr[0] = i_p0p1.xy; pos_arr[1] = i_p0p1.zw;\n"
    "    pos_arr[2] = i_p2p3.xy; pos_arr[3] = i_p2p3.zw;\n"
    "    \n"
    "    vec2 uv_arr[4];\n"
    "    uv_arr[0] = i_uv0uv1.xy; uv_arr[1] = i_uv0uv1.zw;\n"
    "    uv_arr[2] = i_uv2uv3.xy; uv_arr[3] = i_uv2uv3.zw;\n"
    "    \n"
    "    int idx = int(a_corner);\n"
    "    gl_Position = u_ortho * vec4(pos_arr[idx], 0.0, 1.0);\n"
    "    v_texuv = uv_arr[idx];\n"
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
    "                \n"
    "                // OPTIMIZATION 1: pow(x, 2.0) is slower than x * x\n"
    "                float st = smoothstep(0.25, 1.0, luma);\n"
    "                float hdr_weight = st * st;\n"
    "                \n"
    "                float current_boost = mix(1.0, u_raster_hdr_mult, hdr_weight);\n"
    "                vec3 fake_hdr = final_sdr * current_boost;\n"
    "\n"
    "                // Dynamic HDR Desaturation (Highlight Roll-off)\n"
    "                // OPTIMIZATION 2: Luminance scales linearly. No need for a 2nd dot product!\n"
    "                float boosted_luma = luma * current_boost;\n"
    "                vec3 white_mix = vec3(boosted_luma);\n"
    "                float desat_amount = clamp(hdr_weight * (current_boost - 1.0) * 0.10, 0.0, 1.0);\n"
    "                fake_hdr = mix(fake_hdr, white_mix, desat_amount);\n"
    "\n"
    "                // --- HARDWARE DYNAMIC TONE MAPPING (Piecewise Reinhard) ---\n"
    "                float hardware_peak_scRGB = u_device_peak_nits / 80.0;\n"
    "                float max_nits_mult = max(hardware_peak_scRGB, paper_white_mult + 1.0);\n"
    "                vec3 raw_hdr_nits = fake_hdr * paper_white_mult;\n"
    "                \n"
    "                // OPTIMIZATION 3: Mixing with pure white of the same luma doesn't change overall luma. \n"
    "                // No need for a 3rd dot product!\n"
    "                float raw_hdr_luma = boosted_luma * paper_white_mult;\n"
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
 * OPTICAL BLOOM: DOWNSAMPLE & ENERGY EXTRACTION (JIMENEZ 13-TAP)
 * ========================================================================================
 * The first half of the Dual-Filter spatial convolution chain.
 * * Energy Extraction Features:
 * - Continuous Hybrid Luminance: Combines standard Rec.709 perceptual luma with the 
 * dominant phosphor excitation (max RGB channel) using a smooth mix() interpolation. 
 * This solves the "pure blue" threshold crushing issue without introducing mathematical 
 * discontinuities. We use max(RGB) instead of average pixel energy because in a vector 
 * CRT, an intense colored beam is driven entirely by the peak voltage of that specific 
 * phosphor type.
 * - Anti-Popping Knee: Uses a raised knee threshold to ensure moving lines fade smoothly.
 * - Phosphor Saturation Curve: Applies a hybrid quadratic falloff (0.5x + 0.5x^2).
 * ======================================================================================== */
 
static const char* kawase_down_frag_shader_src = 
    "precision mediump float;\n"
    "in highp vec2 v_texuv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D s_texture;\n"
    "uniform vec2 u_texel_size;\n"
    "uniform float u_threshold;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    highp vec2 uv = v_texuv;\n"
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
    "        // --- SMOOTH HYBRID ENERGY EXTRACTION --- \n"
    "        // 1. Calculate standard physical luminance (Rec.709)\n"
    "        float perceptual_luma = dot(color, vec3(0.2126, 0.7152, 0.0722));\n"
    "        \n"
    "        // 2. Calculate the dominant phosphor excitation (peak channel amplitude)\n"
    "        float dominant_phosphor = max(color.r, max(color.g, color.b));\n"
    "        \n"
    "        // 3. Smooth blend. The 0.35 bias towards the dominant phosphor rescues \n"
    "        // inefficient colors (like pure blue) from being crushed by perceptual luma, \n"
    "        // ensuring they still trigger optical blooms like real vector hardware.\n"
    "        float luma = mix(perceptual_luma, dominant_phosphor, 0.35);\n"
    "        \n"
    "        // Anti-Popping Knee\n"
    "        float knee = max(0.04, u_threshold * 0.15);\n"
    "        \n"
    "        // Linear mapping -> Hybrid S-Curve for phosphor saturation\n"
    "        float linear_weight = clamp((luma - (u_threshold - knee)) / (2.0 * knee), 0.0, 1.0);\n"
    "        \n"
    "        color *= linear_weight * (0.5 + 0.5 * linear_weight);\n"
    "    }\n"
    "    fragColor = vec4(color, 1.0);\n"
    "}\n";
	
/* ========================================================================================
 * OPTICAL BLOOM: UPSAMPLE & CRT ASTIGMATISM (OPTIMIZED 4-TAP BILINEAR)
 * ========================================================================================
 * The second half of the Dual-Filter chain. Implements a 9-tap tent filter mathematically 
 * optimized into just 4 hardware bilinear fetches. This smoothly expands the downsampled 
 * light buffers back up without introducing grid artifacts, while drastically reducing GPU 
 * texture bandwidth.
 * * Includes an anisotropic scaling factor (e.g., stretching the X-axis) to emulate 
 * the imperfect magnetic deflection yoke of vintage CRT monitors, creating a classic 
 * horizontal optical flare.
 * ======================================================================================== */
	
static const char* kawase_up_frag_shader_src = 
    "precision mediump float;\n"
    "in highp vec2 v_texuv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D s_texture;\n"
    "uniform vec2 u_texel_size;\n"
    "uniform float u_radius;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    highp vec2 uv = v_texuv;\n"
    "    \n"
    "    // CRT Astigmatism emulation (Anisotropy).\n"
    "    vec2 texel = u_texel_size * vec2(u_radius * 1.2, u_radius * 0.9);\n"
    "    vec2 half_texel = texel * 0.5;\n"
    "\n"
    "    vec3 color = texture(s_texture, uv + vec2(-half_texel.x,  half_texel.y)).rgb;\n"
    "    color += texture(s_texture, uv + vec2( half_texel.x,  half_texel.y)).rgb;\n"
    "    color += texture(s_texture, uv + vec2(-half_texel.x, -half_texel.y)).rgb;\n"
    "    color += texture(s_texture, uv + vec2( half_texel.x, -half_texel.y)).rgb;\n"
    "\n"
    "    // Promediamos las 4 lecturas bilineales (que ya incluyen los 16 pesos matemáticos)\n"
    "    fragColor = vec4(color * 0.25, 1.0);\n"
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
    "    vec3 offscreen_ambient_light = vec3(0.0);\n"
    "    float monitor_glow_shape = 0.0;\n"
    "\n"
    "    if (u_offscreen_glow > 0.0) {\n"
    "        // GPU OPTIMIZATION: Quadratic falloff (Avoids the expensive sqrt() from length())\n"
    "        // Besides massively saving ALU per pixel, the parabolic curve of the \n"
    "        // square is optically more correct for diffuse light than a linear cone.\n"
    "        vec2 delta = v_texuv - vec2(0.5);\n"
    "        \n"
    "        // The maximum dist^2 at the corners is 0.5^2 + 0.5^2 = 0.5. \n"
    "        // Multiplying by 2.0 perfectly normalizes it from 0.0 to 1.0.\n"
    "        float dist_sq = clamp(dot(delta, delta) * 2.0, 0.0, 1.0);\n"
    "        \n"
    "        // 1.0 brightness at the center, falling off smoothly to 0.2 at the edges\n"
    "        monitor_glow_shape = mix(1.0, 0.2, dist_sq);\n"
    "        \n"
    "        // Vintage CRT phosphor tint (slightly blue/cyan/white)\n"
    "        vec3 glow_tint = vec3(0.85, 0.95, 1.0);\n"
    "        offscreen_ambient_light = glow_tint * u_offscreen_glow * monitor_glow_shape;\n"
    "    }\n"
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
    "    if (u_offscreen_glow > 0.0) {\n"
    "        out_mask = max(out_mask, clamp(u_offscreen_glow * monitor_glow_shape, 0.0, 1.0));\n"
    "    }\n"
    "\n"
    "    // Output the mapped color and the carving mask in the alpha channel\n"
    "    fragColor = vec4(mapped, out_mask);\n"
    "}\n";

	