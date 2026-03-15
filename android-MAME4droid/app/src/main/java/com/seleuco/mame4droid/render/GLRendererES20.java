/*
 * This file is part of MAME4droid.
 *
 * Copyright (C) 2026 Filipe Paulino (FlykeSpice)
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

import android.opengl.GLSurfaceView.Renderer;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * OpenGLES 2 renderer for MAME4droid to do hardware accelerated rendering of MAME primitives
 */
public final class GLRendererES20 implements Renderer, IGLRenderer {

	/**
	 * Sets the MAME4droid instance for the renderer.
	 * @param mm The MAME4droid application instance.
	 */
	@Override
	public void setMAME4droid(MAME4droid mm) {
		//this.mm = mm;
	}

	/**
	 * Notifies the renderer that the emulated screen size has changed.
	 * This forces the texture to be re-initialized in the next frame.
	 */
	@Override
	public void changedEmulatedSize() {
		//FlykeSpice: We do nothing
	}

	@Override
	public void onSurfaceCreated(GL10 gl, EGLConfig config) {
		//Call JNI method to do the stuff
		Emulator.onSurfaceCreated();
	}

	@Override
	public void onSurfaceChanged(GL10 gl, int w, int h) {
		//Nothing
	}

	@Override
	synchronized public void onDrawFrame(GL10 gl) {
		//Call JNI method to do GLES2 rendering on native side
		Emulator.onDrawFrame();
	}
}
