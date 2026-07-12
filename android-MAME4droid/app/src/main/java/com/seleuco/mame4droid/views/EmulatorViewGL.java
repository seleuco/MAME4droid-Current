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


package com.seleuco.mame4droid.views;

// Imports of necessary classes from Java and the Android SDK.

import android.content.Context;
import android.graphics.Color;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.helpers.PrefsHelper;
import com.seleuco.mame4droid.render.GLSWRenderer;
import com.seleuco.mame4droid.render.GLNativeRenderer;
import com.seleuco.mame4droid.render.IGLRenderer;
import com.seleuco.mame4droid.widgets.WarnWidget;

import java.util.ArrayList;

/**
 * EmulatorViewGL is a custom view extending GLSurfaceView.
 * Its purpose is to render the emulator's output using OpenGL
 */
public class EmulatorViewGL extends GLSurfaceView implements IEmuView {

	final String TAG = "EmulatorViewGL";

	// Stores the current screen scaling type (e.g., Original, Fullscreen).
	protected int scaleType = PrefsHelper.PREF_ORIGINAL;

	// A reference to the main application class to access helpers and global state.
	protected MAME4droid mm = null;

	// The Renderer object is responsible for drawing the emulator's frames onto the OpenGL surface.
	protected Renderer render = null;

	// A flag to control when the virtual keyboard should be displayed.
	protected boolean showKeyboard = false;

	/**
	 * Returns the instance of the renderer.
	 * @return The current Renderer.
	 */
	public Renderer getRender() {
		return render;
	}

	/**
	 * Returns the current scaling type.
	 * @return An integer representing the scaling type.
	 */
	public int getScaleType() {
		return scaleType;
	}

	/**
	 * Sets the flag to request showing the virtual keyboard.
	 */
	@Override
	public void showSoftKeyboard() {
		showKeyboard = true;
	}

	/**
	 * Sets the scaling type for the view.
	 * @param scaleType The new scaling type.
	 */
	public void setScaleType(int scaleType) {
		this.scaleType = scaleType;
	}

	/**
	 * Assigns the main MAME4droid instance to this view and initializes it.
	 * @param mm The MAME4droid instance.
	 */
	public void setMAME4droid(MAME4droid mm) {
		this.mm = mm;
		init(); // Call the initialization method now that we have the main instance.
		((IGLRenderer)render).setMAME4droid(mm); // Also pass the instance to the renderer.
	}

	/**
	 * Constructor used when creating the view from code.
	 */
	public EmulatorViewGL(Context context) {
		super(context);
	}

	/**
	 * Constructor called when inflating the view from an XML layout.
	 */
	public EmulatorViewGL(Context context, AttributeSet attrs) {
		super(context, attrs);
	}

	/**
	 * Main initialization method for the view.
	 * It configures properties and selects the appropriate OpenGL renderer.
	 */
	protected void init() {
		// Prevents the screen from turning off while the game is active.
		this.setKeepScreenOn(true);
		// Allows the view to receive focus (necessary for keyboard input).
		this.setFocusable(true);
		// Allows the view to receive focus when touched.
		this.setFocusableInTouchMode(true);
		// Request focus immediately.
		this.requestFocus();

		if(mm != null) {
			boolean useHDR = mm.getPrefsHelper().isHDRDisplayEnabled();
			boolean fp16Supported = false;

			// =================================================================
			// PHASE 1: EGL PRE-FLIGHT PROBE on the Main Thread
			// =================================================================
			if (!mm.getPrefsHelper().isShadersEnabled()) {
				// Legacy route (GLES 1)
				android.util.Log.d(TAG, "Initializing context: Legacy Path selected (GLES 1.0).");
				setEGLContextClientVersion(1);
				setEGLConfigChooser(8, 8, 8, 8, 0, 0);
				render = new GLSWRenderer();
			} else if (!useHDR) {
				// Classic route (SDR) without hDR
				android.util.Log.d(TAG, "Initializing context: SDR Path selected (GLES 3.0 / Shaders).");

				setEGLContextClientVersion(3);
				setEGLConfigChooser(8, 8, 8, 8, 0, 0);

				render = new GLNativeRenderer(false, 0);
			} else {

				android.util.Log.d(TAG, "Initializing context: Probing hardware for FP16 support...");

				javax.microedition.khronos.egl.EGL10 egl = (javax.microedition.khronos.egl.EGL10) javax.microedition.khronos.egl.EGLContext.getEGL();
				javax.microedition.khronos.egl.EGLDisplay display = egl.eglGetDisplay(javax.microedition.khronos.egl.EGL10.EGL_DEFAULT_DISPLAY);
				int[] version = new int[2];

				if (egl.eglInitialize(display, version)) {
					final int EGL_OPENGL_ES3_BIT = 64;
					final int EGL_COLOR_COMPONENT_TYPE_EXT = 0x3339;
					final int EGL_COLOR_COMPONENT_TYPE_FLOAT_EXT = 0x333B;

					int[] attribsFp16 = {
						javax.microedition.khronos.egl.EGL10.EGL_RED_SIZE, 16,
						javax.microedition.khronos.egl.EGL10.EGL_GREEN_SIZE, 16,
						javax.microedition.khronos.egl.EGL10.EGL_BLUE_SIZE, 16,
						javax.microedition.khronos.egl.EGL10.EGL_ALPHA_SIZE, 16,
						javax.microedition.khronos.egl.EGL10.EGL_DEPTH_SIZE, 0,
						javax.microedition.khronos.egl.EGL10.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
						EGL_COLOR_COMPONENT_TYPE_EXT, EGL_COLOR_COMPONENT_TYPE_FLOAT_EXT,
						javax.microedition.khronos.egl.EGL10.EGL_NONE
					};

					javax.microedition.khronos.egl.EGLConfig[] configs = new javax.microedition.khronos.egl.EGLConfig[1];
					int[] numConfigs = new int[1];

					// Ask the driver if it has any FP16 configuration
					egl.eglChooseConfig(display, attribsFp16, configs, 1, numConfigs);
					if (numConfigs[0] > 0) {
						fp16Supported = true; // Hardware supports it!
					}

					// We do not call eglTerminate to let GLSurfaceView handle the natural lifecycle
				}


				// =================================================================
				// PHASE 2: SAFE CONFIGURATION AND FALLBACK APPLICATION
				// =================================================================
				if (fp16Supported) {
					android.util.Log.d(TAG, "SUCCESS: Hardware supports FP16. Initializing true HDR path.");

					// 1. Configure the Android window safely (On the Main Thread)
					if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
						if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
							mm.getWindow().setColorMode(android.content.pm.ActivityInfo.COLOR_MODE_HDR);
						} else {
							mm.getWindow().setColorMode(android.content.pm.ActivityInfo.COLOR_MODE_WIDE_COLOR_GAMUT);
						}
					}

					// 2. NOW it is 100% safe to set the 16-bit format in the Holder
					getHolder().setFormat(android.graphics.PixelFormat.RGBA_F16);
					setEGLContextClientVersion(3);

					// 3. Force the selection of the config we already know exists
					setEGLConfigChooser(new EGLConfigChooser() {
						@Override
						public javax.microedition.khronos.egl.EGLConfig chooseConfig(
							javax.microedition.khronos.egl.EGL10 egl, javax.microedition.khronos.egl.EGLDisplay display) {

							int[] attribsFp16 = {
								javax.microedition.khronos.egl.EGL10.EGL_RED_SIZE, 16,
								javax.microedition.khronos.egl.EGL10.EGL_GREEN_SIZE, 16,
								javax.microedition.khronos.egl.EGL10.EGL_BLUE_SIZE, 16,
								javax.microedition.khronos.egl.EGL10.EGL_ALPHA_SIZE, 16,
								javax.microedition.khronos.egl.EGL10.EGL_DEPTH_SIZE, 0,
								javax.microedition.khronos.egl.EGL10.EGL_RENDERABLE_TYPE, 64, // EGL_OPENGL_ES3_BIT
								0x3339, 0x333B, // FLOAT EXTENSIONS
								javax.microedition.khronos.egl.EGL10.EGL_NONE
							};
							javax.microedition.khronos.egl.EGLConfig[] configs = new javax.microedition.khronos.egl.EGLConfig[1];
							int[] numConfigs = new int[1];
							egl.eglChooseConfig(display, attribsFp16, configs, 1, numConfigs);
							return configs[0];
						}
					});

					setEGLWindowSurfaceFactory(new GLSurfaceView.EGLWindowSurfaceFactory() {
						@Override
						public javax.microedition.khronos.egl.EGLSurface createWindowSurface(
							javax.microedition.khronos.egl.EGL10 egl,
							javax.microedition.khronos.egl.EGLDisplay display,
							javax.microedition.khronos.egl.EGLConfig config,
							Object nativeWindow) {

							// Constantes EGL para el Color Space
							final int EGL_GL_COLORSPACE_KHR = 0x309D;
							final int EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT = 0x3350;

							int[] surfaceAttribs = {
								EGL_GL_COLORSPACE_KHR, EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT,
								javax.microedition.khronos.egl.EGL10.EGL_NONE
							};

							android.util.Log.d(TAG, "Creating EGL Surface with explicit scRGB Linear Color Space.");

							javax.microedition.khronos.egl.EGLSurface surface = null;
							try {
								surface = egl.eglCreateWindowSurface(display, config, nativeWindow, surfaceAttribs);
							} catch (IllegalArgumentException e) {
								android.util.Log.e(TAG, "eglCreateWindowSurface failed", e);
							}
							return surface;
						}

						@Override
						public void destroySurface(javax.microedition.khronos.egl.EGL10 egl,
												   javax.microedition.khronos.egl.EGLDisplay display,
												   javax.microedition.khronos.egl.EGLSurface surface) {
							egl.eglDestroySurface(display, surface);
						}
					});


					float maxNits = 300.0f;

					if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
						android.view.Display d = mm.getWindow().getWindowManager().getDefaultDisplay();
						android.view.Display.HdrCapabilities hdrCaps = d.getHdrCapabilities();

						if (hdrCaps != null) {
							float maxNitsDev = hdrCaps.getDesiredMaxLuminance();

							if (maxNitsDev > 100.0f) {
								android.util.Log.d(TAG, "Hardware Max Luminance: " + maxNits + " nits. Dynamic Multiplier: " + maxNits);
								maxNits = maxNitsDev;
							} else {
								android.util.Log.d(TAG, "Warning: Display returned low max nits (" + maxNits + "). Using fallback 3.0.");
							}
						}
					}

					// 4. Instantiate C++ passing the dynamic multiplier
					render = new GLNativeRenderer(true, (int) (maxNits * 100.0f));

				} else {

					android.util.Log.d(TAG, "WARNING: Hardware rejected HDR 16-bit. Falling back to SDR 8-bit.");
					new WarnWidget.WarnWidgetHelper(mm, mm.getString(com.seleuco.mame4droid.R.string.hdr_rejected), 3, Color.RED, false);

					setEGLContextClientVersion(3);
					setEGLConfigChooser(8, 8, 8, 8, 0, 0);

					// C++ will securely know it is in SDR
					render = new GLNativeRenderer(false, 0);

				}
			}

			// Assign everything after having cleaned up the logic
			setRenderer(render);
			setRenderMode(RENDERMODE_WHEN_DIRTY);
		}

	}

	/**
	 * This method is called to determine the size this view should be.
	 * Here, the final size is calculated based on the game's resolution and the chosen scaling type.
	 */
	@Override
	protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
		if (mm == null) {
			// If the app isn't ready, use default dimensions.
			setMeasuredDimension(widthMeasureSpec, heightMeasureSpec);
		} else {
			// If ready, ask the MainHelper to calculate the optimal dimensions.
			ArrayList<Integer> l = mm.getMainHelper().measureWindow(widthMeasureSpec, heightMeasureSpec, scaleType);
            		setMeasuredDimension(l.get(0).intValue(), l.get(1).intValue());
		}
	}

	/**
	 * Called when the size of this view has changed.
	 * We inform the emulator core of the new window size.
	 */
	@Override
	protected void onSizeChanged(int w, int h, int oldw, int oldh) {
		super.onSizeChanged(w, h, oldw, oldh);
		Emulator.setWindowSize(w, h);
	}

	/**
	 * This method must return true for the view to be recognized as a text editor,
	 * which is a prerequisite for the InputMethodManager to show the soft keyboard.
	 * By default, a View returns false.
	 */
	@Override
	public boolean onCheckIsTextEditor() {
		return true;
	}

	/**
	 * This method is the key to enabling keyboard input.
	 * For a GLSurfaceView, the default implementation returns null, telling the system
	 * that this view cannot handle input. We must override it to provide a valid connection.
	 */
	@Override
	public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
		// Prevents the keyboard from going into fullscreen mode in landscape.
		outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_FULLSCREEN;

		// We set the input type to TYPE_NULL. This is crucial for emulators.
		// It tells the keyboard to act like a physical hardware keyboard, sending
		// raw key events (like KEYCODE_W, KEYCODE_SPACE) instead of composing text.
		// This disables autocorrect, suggestions, and other text-composition features.
		outAttrs.inputType = EditorInfo.TYPE_NULL;

		// We return a basic InputConnection. This signals to the Android system
		// that our view is ready to receive keyboard input, allowing the IME to be shown.
		return new BaseInputConnection(this, false);
	}

	/**
	 * Called when the window containing this view gains or loses focus.
	 */
	@Override
	public void onWindowFocusChanged(boolean hasWindowFocus) {
		// It's good practice to call the superclass implementation.
		super.onWindowFocusChanged(hasWindowFocus);

		if(hasWindowFocus) {
			// If mouse support is enabled, capture the pointer.
			if(mm.getPrefsHelper().isMouseEnabled()) {
				this.requestPointerCapture();
			}

			// If a request to show the keyboard is pending...
			if (showKeyboard) {
				// We post the action to the message queue to ensure the view is fully laid out
				// and ready before we try to show the keyboard.
				post(() -> {
					InputMethodManager imm = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
					if (imm != null) {
						// Request to show the soft input (keyboard). The SHOW_FORCED flag
						// is a strong request to display it.
						imm.showSoftInput(this, InputMethodManager.SHOW_IMPLICIT);
						//imm.showSoftInput(this, InputMethodManager.SHOW_FORCED);
						//imm.showSoftInput(this, 0);
					}
					// Reset the flag after the attempt.
					showKeyboard = false;
				});
			}
		}
	}

	/**
	 * Called when a captured pointer event is received.
	 * This is used for mouse input when pointer capture is active.
	 */
	@Override
	public boolean onCapturedPointerEvent(MotionEvent motionEvent) {
		// Forward the event to the input handler.
		return mm.getInputHandler().capturedPointerEvent(motionEvent);
	}
}
