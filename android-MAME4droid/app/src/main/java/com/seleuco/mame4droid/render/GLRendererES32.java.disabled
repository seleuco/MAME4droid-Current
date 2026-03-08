/*
 * This file is part of MAME4droid.
 *
 * Copyright (C) 2025 David Valdeita (Seleuco)
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
 * distribute a such a system following the terms of the GNU GPL for MAME4droid
 * and the licenses of the other code concerned, provided that you include
 * the source code of that other code when and as the GNU GPL requires
 * distribution of source code.
 *
 * Note that people who make modified versions of MAME4idroid are not
 * obligated to grant this special exception for their modified versions; it
 * is their choice whether to do so. The GNU General Public License
 * gives permission to release a modified version which carries forward this exception.
 *
 * MAME4droid is dual-licensed: Alternatively, you can license MAME4droid
 * under a MAME license, as set out in http://mamedev.org/
 */

package com.seleuco.mame4droid.render;

import android.graphics.Color;
import android.opengl.GLES32;
import android.opengl.Matrix;
import android.opengl.GLSurfaceView.Renderer;
import android.util.Log;
import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.widgets.WarnWidget;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class GLRendererES32 implements Renderer, IGLRenderer {

	// Constants for texture filter types
	private static final int FILTER_ON = 1;
	private static final int FILTER_OFF = 2;
	private static final int FILTER_UNDEFINED = 3;

	// Current state of the texture filter
	private int filter = FILTER_UNDEFINED;

	protected int emuTextureId = -1;
	protected ByteBuffer byteBuffer = null;
	protected boolean emuTextureInitialized = false;

	// Indicates if smoothing (linear filtering) is active
	protected boolean smooth = false;

	protected MAME4droid mm = null;
	protected boolean warn = false;

	private static final String TAG = "GLRendererES32";

	private final float[] projectionMatrix = new float[16];
	private int width = 0;
	private int height = 0;
	private int frame = 0;

	private ShaderEffect stockEffect;
	private ShaderEffect currentEffect;

	// Use a constant for the stock shader name to avoid magic strings
	private static final String STOCK_SHADER_NAME = "stock.glsl";
	private static final String NO_EFFECT = "-1";
	private String effectProgramId = NO_EFFECT;

	private final FloatBuffer vertices;
	private final FloatBuffer texcoords;

	private final float[] vertexes_flipped = {
		0, 1,
		1, 1,
		0, 0,
		1, 0
	};

	private final float[] tex_coords = {
		0, 0,
		1, 0,
		0, 1,
		1, 1
	};

	/**
	 * Internal class to store shader configurations.
	 */
	static class ShaderConf {
		ShaderConf(String s, boolean b, int ver) {
			fileName = s;
			smooth = b;
			version = ver;
		}
		String fileName;
		boolean smooth;
		int version;
	}

	LinkedHashMap<Object, Object> shaderConfs = new LinkedHashMap<>();

	/**
	 * Converts a float array to a FloatBuffer.
	 * @param array The float array to convert.
	 * @return The resulting FloatBuffer.
	 */
	protected FloatBuffer convertFloatArrayToFloatBuffer(float[] array) {
		// Allocate buffer based on the size of a float (4 bytes)
		ByteBuffer bb = ByteBuffer.allocateDirect(array.length * 4);
		bb.order(ByteOrder.nativeOrder());
		FloatBuffer fb = bb.asFloatBuffer();
		fb.put(array);
		fb.position(0);
		return fb;
	}

	public void setMAME4droid(MAME4droid mm) {
		this.mm = mm;
		if (mm == null) return;
		fillShaderConfs();
	}

	public GLRendererES32() {
		this.vertices = convertFloatArrayToFloatBuffer(vertexes_flipped);
		this.texcoords = convertFloatArrayToFloatBuffer(tex_coords);
	}

	/**
	 * Called when the emulated screen size changes.
	 */
	public void changedEmulatedSize() {
		if (Emulator.getScreenBuffer() == null) return;
		byteBuffer = Emulator.getScreenBuffer();
		// The texture will need to be reallocated with new dimensions.
		emuTextureInitialized = false;
	}

	@Override
	public void onSurfaceCreated(GL10 gl, EGLConfig config) {
		Log.v(TAG, "onSurfaceCreated ");

		int[] vers = new int[2];
		GLES32.glGetIntegerv(GLES32.GL_MAJOR_VERSION, vers, 0);
		GLES32.glGetIntegerv(GLES32.GL_MINOR_VERSION, vers, 1);
		Log.v(TAG, "glContext major:" + vers[0] + " minor:" + vers[1]);

		// Set GL state once. No need to change these per frame.
		GLES32.glDisable(GLES32.GL_BLEND);
		GLES32.glDisable(GLES32.GL_CULL_FACE);
		GLES32.glDisable(GLES32.GL_DEPTH_TEST);

		GLES32.glClearColor(1.0F, 1.0F, 1.0F, 1.0F); // Normalized

		// Initializes the default (stock) shader
		stockEffect = new ShaderEffect(mm);
		if (!stockEffect.create(STOCK_SHADER_NAME, 1, false)) { // isEffectShader = false
			new WarnWidget.WarnWidgetHelper(mm, "Error creating stock shader!", 5, Color.RED, false);
		}

		// Invalidate resources that depend on the GL context
		emuTextureInitialized = false;
		emuTextureId = -1;
		currentEffect = null;
		effectProgramId = NO_EFFECT;
	}

	@Override
	public void onSurfaceChanged(GL10 gl, int w, int h) {
		Log.v(TAG, "sizeChanged: ==> new Viewport: [" + w + "," + h + "]");
		width = w;
		height = h;
		GLES32.glViewport(0, 0, w, h);
		Matrix.orthoM(this.projectionMatrix, 0, 0, 1, 0, 1, -1, 1);

		// Force re-initialization of the texture in the next frame.
		emuTextureInitialized = false;
	}

	protected boolean isSmooth() {
		return Emulator.isEmuFiltering();
	}

	public void dispose(GL10 gl) {
		// Frees texture and shader resources
		if (emuTextureId != -1) {
			GLES32.glDeleteTextures(1, new int[]{emuTextureId}, 0);
			emuTextureId = -1;
		}
		if (stockEffect != null) {
			stockEffect.dispose();
			stockEffect = null;
		}
		if (currentEffect != null) {
			currentEffect.dispose();
			currentEffect = null;
		}
	}

	/**
	 * Creates or updates the OpenGL ES texture used for rendering the emulator.
	 * Manages filter settings (GL_LINEAR or GL_NEAREST) based on the current state.
	 *
	 * @param requestedFilter The requested filter type (FILTER_ON, FILTER_OFF, FILTER_UNDEFINED).
	 */
	protected void createEmuTexture(int requestedFilter) {
		// Determine the desired filter mode. If an effect shader has a specific requirement,
		// use it. Otherwise, use the global emulator setting.
		boolean smoothFilter;
		if (requestedFilter == FILTER_UNDEFINED) {
			smoothFilter = isSmooth();
		} else {
			smoothFilter = (requestedFilter == FILTER_ON);
		}

		// Check if the texture needs to be created or its filter mode needs to be updated.
		if (emuTextureId == -1 || smooth != smoothFilter) {
			if (emuTextureId != -1) {
				GLES32.glDeleteTextures(1, new int[]{emuTextureId}, 0);
				emuTextureId = -1; // Reset ID immediately after deletion.
			}

			int[] textureUnit = new int[1];
			GLES32.glGenTextures(1, textureUnit, 0);
			emuTextureId = textureUnit[0];

			GLES32.glBindTexture(GLES32.GL_TEXTURE_2D, emuTextureId);

			smooth = smoothFilter;
			int filterParam = smooth ? GLES32.GL_LINEAR : GLES32.GL_NEAREST;
			GLES32.glTexParameteri(GLES32.GL_TEXTURE_2D, GLES32.GL_TEXTURE_MIN_FILTER, filterParam);
			GLES32.glTexParameteri(GLES32.GL_TEXTURE_2D, GLES32.GL_TEXTURE_MAG_FILTER, filterParam);
			//GLES32.glTexParameteri(GLES32.GL_TEXTURE_2D, GLES32.GL_TEXTURE_WRAP_S, GLES32.GL_CLAMP_TO_BORDER);
			//GLES32.glTexParameteri(GLES32.GL_TEXTURE_2D, GLES32.GL_TEXTURE_WRAP_T, GLES32.GL_CLAMP_TO_BORDER);

			GLES32.glTexParameteri(GLES32.GL_TEXTURE_2D, GLES32.GL_TEXTURE_WRAP_S, GLES32.GL_CLAMP_TO_EDGE);
			GLES32.glTexParameteri(GLES32.GL_TEXTURE_2D, GLES32.GL_TEXTURE_WRAP_T, GLES32.GL_CLAMP_TO_EDGE);

			// The texture parameters are set, but it has no data yet.
			// Mark it as uninitialized so it gets created with the correct size later.
			emuTextureInitialized = false;
		}

		if (!emuTextureInitialized) {
			GLES32.glBindTexture(GLES32.GL_TEXTURE_2D, emuTextureId);
			// Allocate storage for the texture. Using null for the data parameter just allocates memory.
			GLES32.glTexImage2D(GLES32.GL_TEXTURE_2D, 0, GLES32.GL_RGBA,
				Emulator.getEmulatedWidth(),
				Emulator.getEmulatedHeight(),
				0, GLES32.GL_RGBA,
				GLES32.GL_UNSIGNED_BYTE, null);
			emuTextureInitialized = true;
		}

		final int error = GLES32.glGetError();
		if (error != GLES32.GL_NO_ERROR) {
			Log.e(TAG, "createEmuTexture GLError: " + error);
		}
	}

	@Override
	//synchronized
	public void onDrawFrame(GL10 unused) {
		try {
			frame++;
			String effectId = mm.getPrefsHelper().getShaderEffectSelected();

			// Load the effect shader if it has changed
			if (!effectId.equals(effectProgramId)) {
				effectProgramId = effectId;

				// Dispose of the previous effect shader
				if (currentEffect != null) {
					currentEffect.dispose();
					currentEffect = null;
				}

				// If a new effect is selected (not NO_EFFECT)
				if (!effectId.equals(NO_EFFECT)) {
					ShaderConf c = (ShaderConf) shaderConfs.get(effectProgramId);
					if (c != null) {
						filter = c.smooth ? FILTER_ON : FILTER_OFF;
						int version = mm.getPrefsHelper().isShadersAs30() ? 3 : c.version;

						currentEffect = new ShaderEffect(mm);
						if (!currentEffect.create(c.fileName, version, true)) { // isEffectShader = true
							currentEffect.dispose(); // Ensure cleanup on failure
							currentEffect = null;
							new WarnWidget.WarnWidgetHelper(mm, "Error creating effect shader... reverting to stock!", 3, Color.RED, false);
						}
					} else {
						new WarnWidget.WarnWidgetHelper(mm, "Not found shader configuration... reverting to stock!", 3, Color.RED, false);
					}
				}
			}

			boolean useEffect = (Emulator.isInGame() || mm.getPrefsHelper().isShadersUsedInFrontend()) && currentEffect != null;

			if (byteBuffer == null) {
				ByteBuffer buf = Emulator.getScreenBuffer();
				if (buf == null) return; // Nothing to draw
				byteBuffer = buf;
			}

			// The buffer's position may have been changed by a previous read.
			byteBuffer.rewind();

			createEmuTexture(useEffect ? filter : FILTER_UNDEFINED);

			GLES32.glClear(GLES32.GL_COLOR_BUFFER_BIT); // No need for depth buffer bit if depth test is disabled.

			ShaderEffect activeEffect = useEffect ? currentEffect : stockEffect;
			if (activeEffect == null || !activeEffect.isProgramReady()) {
				return; // Cannot render if shader program is not valid.
			}

			activeEffect.useProgram();

			// Upload the emulator texture to the GPU
			GLES32.glActiveTexture(GLES32.GL_TEXTURE0);
			GLES32.glBindTexture(GLES32.GL_TEXTURE_2D, emuTextureId);
			GLES32.glUniform1i(activeEffect.getTextureUniformHandle(), 0);
			GLES32.glPixelStorei(GLES32.GL_UNPACK_ALIGNMENT, 1);
			GLES32.glTexSubImage2D(GLES32.GL_TEXTURE_2D, 0, 0, 0,
				Emulator.getEmulatedWidth(), Emulator.getEmulatedHeight(),
				GLES32.GL_RGBA, GLES32.GL_UNSIGNED_BYTE, byteBuffer);
			GLES32.glPixelStorei(GLES32.GL_UNPACK_ALIGNMENT, 4); // restaurar

			// Configure the uniforms of the active shader
			activeEffect.setMVPMatrix(projectionMatrix);
			activeEffect.setUniforms(Emulator.getEmulatedWidth(), Emulator.getEmulatedHeight(), width, height, frame);

			// Draw the quad
			activeEffect.draw(vertices, texcoords);

			ShaderUtil.checkGLError(TAG, "After draw");

		} catch (OutOfMemoryError e) {
			if (!warn) {
				new WarnWidget.WarnWidgetHelper(mm, "Not enough memory to create texture!", 5, Color.RED, false);
				warn = true;
			}
			// Catching generic Exception is better than Throwable, as it avoids catching Errors like OutOfMemoryError twice.
		} catch (Exception e) {
			Log.e(TAG, "Exception during onDrawFrame", e);
		}
	}

	/**
	 * Loads shader configurations from the configuration file.
	 */
	protected void fillShaderConfs() {
		String path = mm.getPrefsHelper().getInstallationDIR();
		ArrayList<ArrayList<String>> data = mm.getMainHelper().readShaderCfg(path);

		for (ArrayList<String> s : data) {
			if (s.size() < 4) continue;
			try {
				shaderConfs.put(s.get(0), new ShaderConf(s.get(0),
					Boolean.parseBoolean(s.get(2)),
					Integer.parseInt(s.get(3))));
			} catch (NumberFormatException ignored) {}
		}

		if (shaderConfs.isEmpty()) {
			new WarnWidget.WarnWidgetHelper(mm, "Error reading shader.cfg file!", 5, Color.RED, false);
		}
	}
}
