/*
 * This file is part of MAME4droid.
 *
 * Copyright (C) 2026 Filipe Paulino (FlykeSpice) & David Valdeita (Seleuco)
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

	private MAME4droid mm;
	private PrefsHelper prefsHelper;
	private boolean init = false;
	private String oldEffect;

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
				mm.runOnUiThread(() -> new WarnWidget.WarnWidgetHelper(mm, "Error creating effect shader... reverting to stock!", 3, Color.RED, false));

				//Reverted to no shaders
				SharedPreferences.Editor editor = prefsHelper.getSharedPreferences().edit();
				editor.putString(PrefsHelper.PREF_SHADER_EFFECT, "none");
				editor.commit();
			}
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
		int res = Emulator.onDrawFrame(Emulator.RENDERER_GL_NATIVE);
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
			init = true;
		}
	}
}
