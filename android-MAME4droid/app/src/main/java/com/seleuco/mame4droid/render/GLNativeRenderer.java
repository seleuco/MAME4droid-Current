/*
 * This file is part of MAME4droid.
 *
 * Copyright (C) 2026 David Valdeita (Seleuco) & Filipe Paulino (FlykeSpice)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses>.
 *
 * Linking MAME4droid statically or dynamically with other modules is
 * making a combined work based on MAME4droid. Thus, the terms and
 * conditions of the GNU General Public License cover the whole
 * combination.
 *
 * In addition, as a special exception, the copyright holders of MAME4droid
 * give you permission to combine MAME4droid with free software programs
 * or libraries that are released under the GNU LGPL and with code included
 * in the standard release of MAME under the MAME License (or modified
 * versions of such code, with unchanged license). You may copy and
 * distribute such a system following the terms of the GNU GPL for MAME4droid
 * and the licenses of the other code concerned, provided that you include
 * the source code of that other code when and as the GNU GPL requires
 * distribution of source code.
 *
 * Note that people who make modified versions of MAME4idroid are not
 * obligated to grant this special exception for their modified versions; it
 * is their choice whether to do so. The GNU General Public License
 * gives permission to release a modified version without this exception;
 * this exception also makes it possible to release a modified version
 * which carries forward this exception.
 *
 * MAME4droid is dual-licensed: Alternatively, you can license MAME4droid
 * under a MAME license, as set out in http://mamedev.org/
 */

package com.seleuco.mame4droid.render;

import android.content.SharedPreferences;
import android.graphics.Color;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView.Renderer;
import android.util.Log;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.helpers.PrefsHelper;
import com.seleuco.mame4droid.widgets.WarnWidget;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * OpenGLES renderer for MAME4droid
 */
public final class GLNativeRenderer implements Renderer, IGLRenderer {

	// =======================================================================
	// CENTRALIZED VECTOR PHYSICS CONSTANTS
	// =======================================================================
	public static final String[] RENDER_KEYS_BOOL = {
		"PREF_VECTOR_EFFECT_FBO_HALF_RES",
		"PREF_VECTOR_EFFECT_BLOOM",
		"PREF_VECTOR_EFFECT_AUTO_EXPOSURE",
		"PREF_VECTOR_EFFECT_OVERBRIGHT",
		"PREF_VECTOR_EFFECT_BEAM_DYNAMICS",
		"PREF_VECTOR_EFFECT_CORNER_BURN",
		"PREF_VECTOR_EFFECT_PHOSPHOR_RESPONSE",
		"PREF_VECTOR_EFFECT_PERSISTENCE",
		"PREF_VECTOR_EFFECT_JITTER",
		"PREF_VECTOR_EFFECT_LINEAR_GAMMA",
		"PREF_HDR_RASTER_FAKE_HDR",
		"PREF_HDR_DIM_VECTOR_ARTWORKS",
		"PREF_VECTOR_EFFECT_OFFSCREEN_GLOW"
	};

	public static final boolean[] DEF_BOOL_VALUES = {
		true,  // HALF_RES
		true,  // BLOOM
		true,  // AUTO_EXPOSURE
		true,  // OVERBRIGHT
		true,  // BEAM_DYNAMICS
		false,  // CORNER_BURN
		false, // PHOSPHOR_RESPONSE
		true,  // PERSISTENCE
		true,  // JITTER
		true,  // LINEAR_GAMMA
		false,  // RASTER_FAKE_HDR
		true,  // DIM_VECTOR_ARTWORKS
		true, // OFFSCREEN_GLOW
	};

	public static final String[] RENDER_KEYS_INT = {
		"PREF_BLOOM_KAWASE_PASSES",
		"PREF_BLOOM_KAWASE_RADIUS",
		"PREF_BLOOM_KAWASE_THRESHOLD",
		"PREF_BLOOM_KAWASE_INTENSITY",
		"PREF_VECTOR_EFFECT_CORE_LINE_WIDTH",
		"PREF_VECTOR_EFFECT_CORE_POINT_WIDTH",
		"PREF_VECTOR_EFFECT_POINT_ENERGY_BOOST",
		"PREF_VECTOR_EFFECT_GLOBAL_DRIVE",
		"PREF_VECTOR_EFFECT_BASE_NITS",
		"PREF_VECTOR_EFFECT_MAX_NITS",
		"PREF_VECTOR_EFFECT_FIXED_EXPOSURE",
		"PREF_VECTOR_EFFECT_AUTO_EXPOSURE_MULT",
		"PREF_VECTOR_EFFECT_AUTO_EXPOSURE_THRESHOLD",
		"PREF_VECTOR_EFFECT_OVERBRIGHT_MAX",
		"PREF_VECTOR_EFFECT_OVERBRIGHT_LINE_MULT",
		"PREF_VECTOR_EFFECT_OVERBRIGHT_POINT_MULT",
		"PREF_VECTOR_EFFECT_OVERBRIGHT_CROSSTALK",
		"PREF_VECTOR_EFFECT_SHORT_LINE_INTENSITY",
		"PREF_VECTOR_EFFECT_SHORT_LINE_WIDTH",
		"PREF_VECTOR_EFFECT_CORNER_DOT_THRESHOLD",
		"PREF_VECTOR_EFFECT_CORNER_BURN_BOOST",
		"PREF_VECTOR_EFFECT_CORNER_BURN_WIDTH_MULT",
		"PREF_VECTOR_EFFECT_PHOSPHOR_BASE_RESPONSE",
		"PREF_VECTOR_EFFECT_PHOSPHOR_LUMA_BOOST",
		"PREF_VECTOR_EFFECT_PHOSPHOR_DECAY",
		"PREF_VECTOR_EFFECT_BEAM_JITTER_AMOUNT",
		"PREF_VECTOR_EFFECT_BEAM_FLICKER_AMOUNT",
		"PREF_HDR_RASTER_HDR_MULTIPLIER",
		"PREF_HDR_RASTER_PAPER_WHITE",
		"PREF_VECTOR_EFFECT_OFFSCREEN_GLOW_MULT"
	};

	public static final int[] DEF_INT_VALUES = {
		3,  // BLOOM_KAWASE_PASSES
		50, // BLOOM_KAWASE_RADIUS
		35, // BLOOM_KAWASE_THRESHOLD
		44, // BLOOM_KAWASE_INTENSITY
		50, // BLOOM_CORE_LINE_WIDTH
		30, // BLOOM_CORE_POINT_WIDTH
		15, // BLOOM_POINT_ENERGY_BOOST
		40, // GLOBAL_DRIVE
		15, // BASE_NITS (250 nits)
		60, // MAX_NITS (700 nits)
		35, // FIXED_EXPOSURE (1.2f)
		40, // AUTO_EXPOSURE_MULT
		50, // AUTO_EXPOSURE_THRESHOLD
		50, // OVERBRIGHT_MAX
		40, // OVERBRIGHT_LINE_MULT
		55, // OVERBRIGHT_POINT_MULT
		25, // OVERBRIGHT_CROSSTALK
		30, // SHORT_LINE_INTENSITY
		10, // SHORT_LINE_WIDTH
		50, // CORNER_DOT_THRESHOLD
		10, // CORNER_BURN_BOOST
		50, // CORNER_BURN_WIDTH_MULT
		40, // PHOSPHOR_BASE_RESPONSE
		60, // PHOSPHOR_LUMA_BOOST
		20, // PHOSPHOR_DECAY
		15, // BEAM_JITTER_AMOUNT
		20, // BEAM_FLICKER_AMOUNT
		35, // RASTER_HDR_MULTIPLIER
		25,  // PREF_HDR_RASTER_PAPER_WHITE
		15,  // OFFSCREEN_GLOW_MULT
	};

	private MAME4droid mm;
	private PrefsHelper prefsHelper;
	private boolean init = false;
	private String oldEffect;
	private boolean isHdr;
	private int maxnits;

	public GLNativeRenderer(boolean hdr, int nits){
		isHdr = hdr;
		maxnits = nits;
	}

	/**
	 * Sets the MAME4droid instance for the renderer.
	 * @param mm The MAME4droid application instance.
	 */
	@Override
	public void setMAME4droid(MAME4droid mm) {
		this.mm = mm;
		if(mm!=null) {
			prefsHelper = mm.getPrefsHelper();
			oldEffect = prefsHelper.getShaderEffectSelected();
		}
	}

	/**
	 * Notifies the renderer that the emulated screen size has changed.
	 * This forces the texture to be re-initialized in the next frame.
	 */
	@Override
	public void changedEmulatedSize() {
	}

	private void updateShaderEffect() {
		if(prefsHelper == null)
			return;
		String effect = prefsHelper.isShadersEnabled() ? prefsHelper.getShaderEffectSelected() : "none";

		if (!oldEffect.equals(effect)) {
			oldEffect = effect;

			boolean ret;
			if (effect.equals("none"))
				ret = Emulator.setShader(null);
			else
				ret = Emulator.setShader(effect);

			if (!ret) {
				mm.runOnUiThread(() -> new WarnWidget.WarnWidgetHelper(mm, mm.getString(com.seleuco.mame4droid.R.string.shader_error), 3, Color.RED, false));

				//Reverted to no shaders
				SharedPreferences.Editor editor = prefsHelper.getSharedPreferences().edit();
				editor.putString(PrefsHelper.PREF_SHADER_EFFECT, "none");
				editor.commit();
			}
		}
	}

	/**
	 * Sends all current SharedPreferences to the C++ Renderer
	 */
	public static void syncRendererParameters(SharedPreferences prefs) {
		int totalSize = RENDER_KEYS_BOOL.length + RENDER_KEYS_INT.length;
		String[] keys = new String[totalSize];
		String[] values = new String[totalSize];

		int idx = 0;

		for (int i = 0; i < RENDER_KEYS_BOOL.length; i++) {
			keys[idx] = RENDER_KEYS_BOOL[i];
			values[idx] = prefs.getBoolean(RENDER_KEYS_BOOL[i], DEF_BOOL_VALUES[i]) ? "1" : "0";
			idx++;
		}

		for (int i = 0; i < RENDER_KEYS_INT.length; i++) {
			keys[idx] = RENDER_KEYS_INT[i];
			values[idx] = String.valueOf(prefs.getInt(RENDER_KEYS_INT[i], DEF_INT_VALUES[i]));
			idx++;
		}

		Emulator.setRendererParameters(keys, values);
	}

	/**
	 * Hard-resets all Vector parameters to their optimal Arcade defaults
	 */
	public static void restoreVectorDefaults(SharedPreferences prefs) {
		SharedPreferences.Editor editor = prefs.edit();

		for (int i = 0; i < RENDER_KEYS_BOOL.length; i++) {
			if (RENDER_KEYS_BOOL[i].startsWith("PREF_HDR_")) continue;
			editor.putBoolean(RENDER_KEYS_BOOL[i], DEF_BOOL_VALUES[i]);
		}

		for (int i = 0; i < RENDER_KEYS_INT.length; i++) {
			String key = RENDER_KEYS_INT[i];
			if (key.equals("PREF_VECTOR_EFFECT_BASE_NITS") ||
				key.equals("PREF_VECTOR_EFFECT_MAX_NITS") ||
				key.startsWith("PREF_HDR_")) {
				continue;
			}
			editor.putInt(key, DEF_INT_VALUES[i]);
		}
		editor.commit();

		if (Emulator.isEmulating()) {
			syncRendererParameters(prefs);
		}
	}

	public static void restoreHDRDefaults(SharedPreferences prefs) {
		SharedPreferences.Editor editor = prefs.edit();

		for (int i = 0; i < RENDER_KEYS_BOOL.length; i++) {
			if (RENDER_KEYS_BOOL[i].startsWith("PREF_HDR_")) {
				editor.putBoolean(RENDER_KEYS_BOOL[i], DEF_BOOL_VALUES[i]);
			}
		}

		for (int i = 0; i < RENDER_KEYS_INT.length; i++) {
			String key = RENDER_KEYS_INT[i];
			if (key.equals("PREF_BLOOM_BASE_NITS") ||
				key.equals("PREF_BLOOM_MAX_NITS") ||
				key.startsWith("PREF_HDR_")) {
				editor.putInt(key, DEF_INT_VALUES[i]);
			}
		}
		editor.commit();

		if (Emulator.isEmulating()) {
			syncRendererParameters(prefs);
		}
	}

	@Override
	public void onSurfaceCreated(GL10 gl, EGLConfig config) {
		//Call JNI method to do initialization stuff
		Log.d("GLRENDERER", "onSurfaceCreated called");
		Emulator.newRenderer();
	}

	@Override
	public void onSurfaceChanged(GL10 gl, int w, int h) {
		Log.d("GLRENDERER", "onSurfaceChanged called");
		GLES20.glViewport(0, 0, w, h);
		//This is called when you exit from the Preferences screen
		updateShaderEffect();
	}

	@Override
	public void onDrawFrame(GL10 gl) {
		//Call JNI method to do GLES rendering on native side
		int res = Emulator.onDrawFrame(Emulator.RENDERER_GL_NATIVE, isHdr? maxnits : 0);
		if(res==-1)
		{
			gl.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			//gl.glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
			gl.glClear(GL10.GL_COLOR_BUFFER_BIT);
		}
		else if(!init && mm != null)
		{
			Emulator.loadShaders(mm.getMainHelper().getInstallationDIR());
			Emulator.setShader(oldEffect.equals("none") ? null : oldEffect);
			syncRendererParameters(prefsHelper.getSharedPreferences());
			init = true;
		}
	}
}
