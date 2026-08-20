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

import java.util.ArrayList;

import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Paint.Style;
import android.graphics.Rect;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.util.AttributeSet;
import android.widget.ImageView;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.R;
import com.seleuco.mame4droid.helpers.PrefsHelper;
import com.seleuco.mame4droid.input.ControlCustomizer;
import com.seleuco.mame4droid.input.IController;
import com.seleuco.mame4droid.input.InputHandler;
import com.seleuco.mame4droid.input.InputValue;
import com.seleuco.mame4droid.input.TiltSensor;
import com.seleuco.mame4droid.input.TouchController;

/**
 * InputView is a custom View responsible for rendering the on-screen virtual controller
 * (D-pad, buttons, etc.) and displaying their current state (e.g., pressed or unpressed).
 * It extends ImageView to potentially support a background image for the controller area.
 */
public class InputView extends ImageView {

	// A reference to the main application class for accessing global helpers and state.
	protected MAME4droid mm = null;
	// The background bitmap for the view, if any.
	protected Bitmap bmp = null;
	// A reusable Paint object for drawing operations (e.g., debug borders).
	protected Paint pnt = new Paint();
	// Reusable Rect objects to avoid memory allocation during drawing.
	protected Rect rsrc = new Rect();
	protected Rect rdst = new Rect();
	protected Rect rclip = new Rect();
	// Offset for positioning the controls within the view (for centering/letterboxing).
	protected int ax = 0;
	protected int ay = 0;
	// Scaling factors to map the abstract controller layout to the actual screen pixels.
	protected float dx = 1;
	protected float dy = 1;

	// Static arrays to hold drawable resources for the stick and buttons.
	// 'static' is used as an optimization so these images are loaded only ONCE for the
	// entire application lifecycle, not every time a new game is started.
	static BitmapDrawable stick_images[] = null;
	static BitmapDrawable btns_images[][] = null;

	/**
	 * Sets the main application context and initializes the view's resources.
	 * This is the primary setup method for the view.
	 * @param mm The main MAME4droid application instance.
	 */
	public void setMAME4droid(MAME4droid mm) {
		this.mm = mm;
		if (mm == null) return;

		// Lazily initialize the stick images. This block runs only once.
		if (stick_images == null) {
			stick_images = new BitmapDrawable[9]; // 8 directions + neutral
			stick_images[IController.STICK_DOWN] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.dpad_down);
			stick_images[IController.STICK_DOWN_LEFT] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.dpad_down_left);
			stick_images[IController.STICK_DOWN_RIGHT] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.dpad_down_right);
			stick_images[IController.STICK_LEFT] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.dpad_left);
			stick_images[IController.STICK_NONE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.dpad_none);
			stick_images[IController.STICK_RIGHT] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.dpad_right);
			stick_images[IController.STICK_UP] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.dpad_up);
			stick_images[IController.STICK_UP_LEFT] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.dpad_up_left);
			stick_images[IController.STICK_UP_RIGHT] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.dpad_up_right);
		}

		// Lazily initialize the button images. This block also runs only once.
		if (btns_images == null) {
			btns_images = new BitmapDrawable[IController.NUM_BUTTONS][2]; // For each button, 2 states: pressed and not pressed.

			// Load drawables for button A (normal and pressed states)
			btns_images[IController.BTN_1][IController.BTN_NO_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_1);
			btns_images[IController.BTN_1][IController.BTN_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_1_press);

			// ... and so on for all other buttons (B, C, D, E, F, G, H, Exit, Option, Start, Coin) ...
			btns_images[IController.BTN_2][IController.BTN_NO_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_2);
			btns_images[IController.BTN_2][IController.BTN_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_2_press);
			btns_images[IController.BTN_3][IController.BTN_NO_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_3);
			btns_images[IController.BTN_3][IController.BTN_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_3_press);
			btns_images[IController.BTN_4][IController.BTN_NO_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_4);
			btns_images[IController.BTN_4][IController.BTN_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_4_press);
			btns_images[IController.BTN_5][IController.BTN_NO_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_5);
			btns_images[IController.BTN_5][IController.BTN_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_5_press);
			btns_images[IController.BTN_6][IController.BTN_NO_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_6);
			btns_images[IController.BTN_6][IController.BTN_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_6_press);
			btns_images[IController.BTN_7][IController.BTN_NO_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_7);
			btns_images[IController.BTN_7][IController.BTN_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_7_press);
			btns_images[IController.BTN_8][IController.BTN_NO_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_8);
			btns_images[IController.BTN_8][IController.BTN_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_8_press);
			btns_images[IController.BTN_EXIT][IController.BTN_NO_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_exit);
			btns_images[IController.BTN_EXIT][IController.BTN_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_exit_press);
			btns_images[IController.BTN_OPTION][IController.BTN_NO_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_option);
			btns_images[IController.BTN_OPTION][IController.BTN_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_option_press);
			btns_images[IController.BTN_START][IController.BTN_NO_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_start);
			btns_images[IController.BTN_START][IController.BTN_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_start_press);
			btns_images[IController.BTN_COIN][IController.BTN_NO_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_coin);
			btns_images[IController.BTN_COIN][IController.BTN_PRESS_STATE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.button_coin_press);
		}
	}

	/** Standard View constructor */
	public InputView(Context context) {
		super(context);
		init();
	}

	/** Standard View constructor called when inflating from XML */
	public InputView(Context context, AttributeSet attrs) {
		super(context, attrs);
		init();
	}

	/** Standard View constructor */
	public InputView(Context context, AttributeSet attrs, int defStyle) {
		super(context, attrs, defStyle);
		init();
	}

	/**
	 * Performs basic initialization for the view.
	 */
	protected void init() {
		// Configure the Paint object for drawing debug outlines.
		pnt.setARGB(255, 255, 255, 255);
		pnt.setStyle(Style.STROKE);
		pnt.setTextSize(16);

		// Make the view focusable to potentially receive key events.
		this.setFocusable(true);
		this.setFocusableInTouchMode(true);
	}

	/**
	 * Overrides the default method to get a reference to the underlying Bitmap
	 * if a background drawable is set.
	 */
	@Override
	public void setImageDrawable(Drawable drawable) {
		if (drawable != null) {
			bmp = ((BitmapDrawable) drawable).getBitmap();
		} else {
			bmp = null;
		}
		super.setImageDrawable(drawable);
	}

	/**
	 * Calculates the dimensions of the view, ensuring the aspect ratio of the
	 * controller layout is maintained.
	 */
	@Override
	protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
		if (mm == null) {
			super.onMeasure(widthMeasureSpec, heightMeasureSpec);
			return;
		}

		int widthSize;
		int heightSize;

		// In landscape, the view typically takes up the full screen height and a portion of the width.
		if (mm.getMainHelper().getscrOrientation() == Configuration.ORIENTATION_LANDSCAPE) {
			widthSize = MeasureSpec.getSize(widthMeasureSpec);
			heightSize = MeasureSpec.getSize(heightMeasureSpec);
		} else { // In portrait, the controls are usually in a box below the game screen.
			// Get the 'native' size of the controller layout.
			int w = 1;
			int h = 1;
			if (mm.getInputHandler().getTouchController().getMainRect() != null) {
				w = mm.getInputHandler().getTouchController().getMainRect().width();
				h = mm.getInputHandler().getTouchController().getMainRect().height();
			}
			if (w == 0) w = 1;
			if (h == 0) h = 1;

			// Calculate height based on width to maintain the aspect ratio.
			float desiredAspect = (float) w / (float) h;
			widthSize = mm.getWindowManager().getDefaultDisplay().getWidth();
			heightSize = (int) (widthSize / desiredAspect);
		}

		// Set the final calculated dimensions for this view.
		setMeasuredDimension(widthSize, heightSize);
	}

	/**
	 * Prepares all the drawable images by setting their size, position (bounds), and
	 * transparency (alpha) based on the current layout configuration.
	 */
	public void updateImages() {
		if (mm == null) return;
		ArrayList<InputValue> data = mm.getInputHandler().getTouchController().getAllInputData();
		if (data == null) return;

		int controllerAlpha = mm.getMainHelper().getControllerAlpha();

		// Loop through every defined input element (stick, buttons).
		for (InputValue v : data) {
			if (v.getType() == TouchController.TYPE_STICK_IMG) {
				// Set the bounds and alpha for all 9 stick direction images.
				for (BitmapDrawable stick_image : stick_images) {
					stick_image.setBounds(v.getRect());
					stick_image.setAlpha(controllerAlpha);
				}
			} else if (v.getType() == TouchController.TYPE_BUTTON_IMG) {
				// Set the bounds and alpha for both the pressed and unpressed states of this button.
				btns_images[v.getValue()][IController.BTN_PRESS_STATE].setBounds(v.getRect());
				btns_images[v.getValue()][IController.BTN_PRESS_STATE].setAlpha(controllerAlpha);
				btns_images[v.getValue()][IController.BTN_NO_PRESS_STATE].setBounds(v.getRect());
				btns_images[v.getValue()][IController.BTN_NO_PRESS_STATE].setAlpha(controllerAlpha);
			}
		}
	}

	/**
	 * Called when the size of this view has changed. This is where we calculate the
	 * final scaling factors and offsets needed to draw the controls correctly.
	 */
	@Override
	protected void onSizeChanged(int w, int h, int oldw, int oldh) {
		super.onSizeChanged(w, h, oldw, oldh);

		// Get the 'native' size of the controller layout from the TouchController.
		int bw = 1;
		int bh = 1;
		if (mm != null && mm.getInputHandler().getTouchController().getMainRect() != null) {
			bw = mm.getInputHandler().getTouchController().getMainRect().width();
			bh = mm.getInputHandler().getTouchController().getMainRect().height();
		}
		if (bw == 0) bw = 1;
		if (bh == 0) bh = 1;

		// Calculate the aspect ratio of the controller layout.
		float desiredAspect = (float) bw / (float) bh;

		// Fit the layout within the view's new dimensions (w, h), preserving the aspect ratio.
		// This calculates letterboxing/pillarboxing offsets (ax, ay).
		int tmp = (int) ((float) w / desiredAspect);
		if (tmp <= h) { // Letterbox (space on top/bottom)
			ax = 0;
			ay = (h - tmp) / 2;
			h = tmp;
		} else { // Pillarbox (space on left/right)
			tmp = (int) ((float) h * desiredAspect);
			ay = 0;
			ax = (w - tmp) / 2;
			w = tmp;
		}

		// Calculate the final scaling factors.
		dx = (float) w / (float) bw;
		dy = (float) h / (float) bh;

		if (mm == null || mm.getInputHandler() == null) return;

		// Inform the TouchController about the transformation factors so it can correctly
		// map screen touch coordinates to virtual controller coordinates.
		mm.getInputHandler().getTouchController().setFixFactor(ax, ay, dx, dy);

		// Now that sizes and positions are known, update the drawables.
		updateImages();
	}

	/**
	 * The main drawing method. This is called by the Android system whenever the
	 * view needs to be redrawn.
	 */
	@Override
	protected void onDraw(Canvas canvas) {
		// Draw the optional background image first.
		if (bmp != null) {
			super.onDraw(canvas);
		}

		if (mm == null) return;

		ArrayList<InputValue> data = mm.getInputHandler().getTouchController().getAllInputData();
		if (data == null) return;

		// Iterate through all configured input elements.
		for (InputValue v : data) {
			BitmapDrawable d = null;
			canvas.getClipBounds(rclip); // Get the visible area of the canvas.

			// Check if this specific control is configured to be visible.
			if (!mm.getInputHandler().getTouchController().isHandledTouchItem(v)) {
				continue;
			}

			// Optimization: only process elements that are within the visible area.
			if (v.getRect() != null && rclip.intersect(v.getRect())) {
				if (v.getType() == TouchController.TYPE_STICK_IMG) {
					// Select the correct stick image based on the current input state.
					d = stick_images[mm.getInputHandler().getTouchController().getStick_state()];
				} else if (v.getType() == TouchController.TYPE_ANALOG_RECT) {
					// If it's an analog stick, delegate drawing to its own class.
					mm.getInputHandler().getTouchStick().draw(canvas);
				} else if (v.getType() == TouchController.TYPE_BUTTON_IMG) {
					// Select the correct button image (pressed or unpressed).
					d = btns_images[v.getValue()][mm.getInputHandler().getTouchController().getBtnStates()[v.getValue()]];
				}
			}

			// If a drawable was selected, draw it.
			if (d != null) {
				d.draw(canvas);
			}
		}

		// If the control customizer is active, draw its overlay on top.
		if (ControlCustomizer.isEnabled()) {
			mm.getInputHandler().getControlCustomizer().draw(canvas);
		}

		// If in debug mode, draw outlines of the touchable areas.
		if (Emulator.isDebug()) {
			ArrayList<InputValue> ids = mm.getInputHandler().getTouchController().getAllInputData();
			Paint p2 = new Paint();
			p2.setARGB(255, 255, 255, 255);
			p2.setStyle(Style.STROKE);

			for (InputValue v : ids) {
				Rect r = v.getRect();
				if (r != null) {
					// Draw rectangles for buttons and the currently active stick type.
					if (v.getType() == TouchController.TYPE_BUTTON_RECT) canvas.drawRect(r, p2);
					else if (mm.getPrefsHelper().getControllerType() == PrefsHelper.PREF_DIGITAL_DPAD && v.getType() == TouchController.TYPE_STICK_RECT) canvas.drawRect(r, p2);
					else if (mm.getPrefsHelper().getControllerType() != PrefsHelper.PREF_DIGITAL_DPAD && v.getType() == TouchController.TYPE_ANALOG_RECT) canvas.drawRect(r, p2);
				}
			}

			// Draw tilt sensor debug info if enabled.
			p2.setTextSize(30);
			if (mm.getInputHandler().getTiltSensor().isEnabled() && TiltSensor.str != null) {
				canvas.drawText(TiltSensor.str, 100, 150, p2);
			}
		}
	}
}
