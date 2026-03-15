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
import java.util.ArrayList;
import android.content.Context;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.view.MotionEvent;
// Imports required for the Input Method Framework (soft keyboard).
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

// Imports of MAME4droid specific classes.
import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.render.GLRendererES20;
//import com.seleuco.mame4droid.render.GLRendererES32;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.helpers.PrefsHelper;
import com.seleuco.mame4droid.render.IGLRenderer;

/**
 * EmulatorViewGL is a custom view extending GLSurfaceView.
 * Its purpose is to render the emulator's output using OpenGL
 */
public class EmulatorViewGL extends GLSurfaceView implements IEmuView {

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
			// Check if shaders are enabled in the preferences.
			//if (mm.getPrefsHelper().isShadersEnabled()) {
			//	// If so, use OpenGL ES 3.2 for advanced effects.
			//	setEGLContextClientVersion(3);
			//	render = new GLRendererES32();
			//} else {
				// Otherwise, use the more faster GLES2
				setEGLContextClientVersion(2);
				render = new GLRendererES20();
			//}

			// Assign the renderer to the GLSurfaceView.
			setRenderer(render);
			// Set the render mode to 'WHEN_DIRTY'.
			// This means the view is only redrawn when we explicitly request it
			// (e.g., when a new frame arrives from the emulator), which saves battery.
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
