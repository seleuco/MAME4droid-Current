/*
 * This file is part of MAME4droid.
 *
 * Copyright (C) 2026 David Valdeita (Seleuco)
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

package com.seleuco.mame4droid.input;

import java.util.ArrayList;
import android.content.res.Configuration;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Paint.Style;
import android.graphics.Typeface;
import android.view.MotionEvent;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.helpers.DialogHelper;
import com.seleuco.mame4droid.helpers.PrefsHelper;

public class ControlCustomizer {

	private static boolean enabled = false;

	// Constant for the movement snap-to-grid threshold to keep layouts clean
	private static final int SNAP_TO_GRID = 5;

	// Coordinates of the initial touch position
	private int initialTouchX = 0;
	private int initialTouchY = 0;

	// Initial offset of the control when it starts to be dragged
	private int initialXOffset = 0;
	private int initialYOffset = 0;

	// Tracks the active pointer ID to prevent multi-touch warping during drags
	private int activePointerId = -1;

	private InputValue valueMoved = null;

	protected MAME4droid mm = null;

	// Cached UI objects to prevent Garbage Collection churn during the 60fps draw loop
	private Rect saveButtonRect = new Rect();
	private Paint pButton;
	private Paint pText;
	private Paint pRect;
	private boolean paintsInitialized = false;

	// Button dimensions
	private static final int BUTTON_WIDTH = 200;
	private static final int BUTTON_HEIGHT = 80;
	private static final int TEXT_PADDING = 10;

	public static void setEnabled(boolean enabled) {
		ControlCustomizer.enabled = enabled;
	}

	public static boolean isEnabled() {
		return enabled;
	}

	public void setMAME4droid(MAME4droid value) {
		mm = value;
	}

	/**
	 * Discards the position changes of the control layout.
	 * Resets temporary offsets to zero.
	 */
	public void discardDefinedControlLayout() {
		ArrayList<InputValue> values = mm.getInputHandler().getTouchController().getAllInputData();
		for (InputValue iv : values) {
			iv.setOffsetTMP(0, 0);
			if (iv.getType() == TouchController.TYPE_ANALOG_RECT) {
				mm.getInputHandler().getTouchStick().setStickArea(iv.getRect());
			}
		}
		mm.getInputView().updateImages();
	}

	/**
	 * Commits and saves the current temporary position offsets to persistent storage.
	 */
	public void saveDefinedControlLayout() {
		StringBuilder definedStr = new StringBuilder();
		ArrayList<InputValue> values = mm.getInputHandler().getTouchController().getAllInputData();
		boolean first = true;

		for (InputValue iv : values) {
			// Commit temporary offset changes to permanent offsets
			iv.commitChanges();
			if (iv.getXoff() != 0 || iv.getYoff() != 0) {
				if (!first) {
					definedStr.append(",");
				}
				definedStr.append(iv.getType()).append(",")
					.append(iv.getValue()).append(",")
					.append(iv.getXoff()).append(",")
					.append(iv.getYoff());
				first = false;
			}
		}

		if (mm.getMainHelper().getscrOrientation() == Configuration.ORIENTATION_LANDSCAPE) {
			mm.getPrefsHelper().setDefinedControlLayoutLand(definedStr.toString());
		} else {
			mm.getPrefsHelper().setDefinedControlLayoutPortrait(definedStr.toString());
		}
	}

	/**
	 * Restores layout offsets from SharedPreferences and applies them to the UI components.
	 */
	public void readDefinedControlLayout() {
		// Do not apply custom offsets if in non-full portrait mode to prevent clipping
		if (mm.getMainHelper().getscrOrientation() == Configuration.ORIENTATION_PORTRAIT && !Emulator.isPortraitFull()) {
			return;
		}

		ArrayList<InputValue> values = mm.getInputHandler().getTouchController().getAllInputData();
		String definedStr = (mm.getMainHelper().getscrOrientation() == Configuration.ORIENTATION_LANDSCAPE)
			? mm.getPrefsHelper().getDefinedControlLayoutLand()
			: mm.getPrefsHelper().getDefinedControlLayoutPortrait();

		if (definedStr != null && !definedStr.isEmpty()) {
			String[] tokens = definedStr.split(",");

			// Failsafe: Ensure data stream is intact (Type, Value, X, Y chunks)
			if (tokens.length % 4 != 0) return;

			for (int i = 0; i < tokens.length; i += 4) {
				try {
					int type = Integer.parseInt(tokens[i]);
					int value = Integer.parseInt(tokens[i + 1]);
					int xoff = Integer.parseInt(tokens[i + 2]);
					int yoff = Integer.parseInt(tokens[i + 3]);

					for (InputValue iv : values) {
						if (iv.getType() == type && iv.getValue() == value) {
							iv.setOffset(xoff, yoff);
							if (type == TouchController.TYPE_ANALOG_RECT) {
								mm.getInputHandler().getTouchStick().setStickArea(iv.getRect());
							}
							break;
						}
					}
				} catch (NumberFormatException e) {
					e.printStackTrace();
				}
			}
		}
		mm.getInputView().updateImages();
	}

	/**
	 * Synchronizes related bounding boxes (e.g., visual image bounds vs logical touch bounds)
	 * for the actively dragged control.
	 */
	protected void updateRelatedRects() {
		if (valueMoved == null) return;

		ArrayList<InputValue> values = mm.getInputHandler().getTouchController().getAllInputData();

		if (valueMoved.getType() == TouchController.TYPE_BUTTON_RECT) {
			for (InputValue iv : values) {
				if (iv.getType() == TouchController.TYPE_BUTTON_IMG && iv.getValue() == valueMoved.getValue()) {
					iv.setOffsetTMP(valueMoved.getXoff_tmp(), valueMoved.getYoff_tmp());
					break;
				}
			}
		} else if (valueMoved.getType() == TouchController.TYPE_STICK_IMG || valueMoved.getType() == TouchController.TYPE_ANALOG_RECT) {
			for (InputValue iv : values) {
				if (iv.getType() == TouchController.TYPE_STICK_RECT ||
					iv.getType() == TouchController.TYPE_STICK_IMG ||
					iv.getType() == TouchController.TYPE_ANALOG_RECT) {
					iv.setOffsetTMP(valueMoved.getXoff_tmp(), valueMoved.getYoff_tmp());
				}
				if (iv.getType() == TouchController.TYPE_ANALOG_RECT) {
					mm.getInputHandler().getTouchStick().setStickArea(valueMoved.getRect());
				}
			}
		}
	}

	/**
	 * Translates user touch input into layout coordinate offsets.
	 */
	public void handleMotion(MotionEvent event) {
		int action = event.getActionMasked();
		int pointerIndex = event.getActionIndex();

		switch (action) {
			case MotionEvent.ACTION_DOWN:
				activePointerId = event.getPointerId(0);
				int x = (int) event.getX(0);
				int y = (int) event.getY(0);

				// Check if the save button was touched
				if (saveButtonRect != null && saveButtonRect.contains(x, y)) {
					mm.showDialog(DialogHelper.DIALOG_FINISH_CUSTOM_LAYOUT);
					mm.getInputView().invalidate();
					return;
				}

				// Find the targeted control for dragging
				ArrayList<InputValue> values = mm.getInputHandler().getTouchController().getAllInputData();
				for (InputValue iv : values) {
					if ((iv.getType() == TouchController.TYPE_BUTTON_RECT ||
						iv.getType() == TouchController.TYPE_STICK_IMG ||
						iv.getType() == TouchController.TYPE_ANALOG_RECT)
						&& iv.getRect().contains(x, y)) {

						valueMoved = iv;
						initialTouchX = x;
						initialTouchY = y;
						initialXOffset = iv.getXoff_tmp();
						initialYOffset = iv.getYoff_tmp();
						break;
					}
				}
				break;

			case MotionEvent.ACTION_MOVE:
				if (valueMoved != null) {
					// Enforce pointer lock: ignore secondary fingers sliding around
					int pIndex = event.findPointerIndex(activePointerId);
					if (pIndex != -1) {
						int currX = (int) event.getX(pIndex);
						int currY = (int) event.getY(pIndex);

						int deltaX = currX - initialTouchX;
						int deltaY = currY - initialTouchY;

						// Apply snap-to-grid adjustment
						int newXOffset = initialXOffset + (deltaX / SNAP_TO_GRID) * SNAP_TO_GRID;
						int newYOffset = initialYOffset + (deltaY / SNAP_TO_GRID) * SNAP_TO_GRID;

						valueMoved.setOffsetTMP(newXOffset, newYOffset);
						updateRelatedRects();

						if (deltaX != 0 || deltaY != 0) {
							mm.getInputView().updateImages();
							mm.getInputView().invalidate();
						}
					}
				}
				break;

			case MotionEvent.ACTION_UP:
			case MotionEvent.ACTION_POINTER_UP:
				if (event.getPointerId(pointerIndex) == activePointerId) {
					valueMoved = null;
					activePointerId = -1;
					mm.getInputView().invalidate();
				}
				break;

			case MotionEvent.ACTION_CANCEL:
				valueMoved = null;
				activePointerId = -1;
				mm.getInputView().invalidate();
				break;
		}
	}

	/**
	 * Lazy-load Paint objects to ensure strict memory constraints on the UI thread.
	 */
	private void initPaints() {
		if (paintsInitialized) return;

		pButton = new Paint();
		pButton.setColor(Color.YELLOW);
		pButton.setStyle(Style.FILL);
		pButton.setAlpha(200);

		pText = new Paint();
		pText.setColor(Color.BLACK);
		pText.setTextSize(30);
		pText.setTypeface(Typeface.DEFAULT_BOLD);
		pText.setTextAlign(Paint.Align.CENTER);

		pRect = new Paint();
		pRect.setARGB(30, 255, 255, 255);
		pRect.setStyle(Style.FILL);

		paintsInitialized = true;
	}

	public void draw(Canvas canvas) {
		if (canvas == null) return;

		// Ensure rendering tools are primed
		initPaints();

		int centerX = canvas.getWidth() / 2;
		int centerY = canvas.getHeight() / 2;

		// Define Save Button bounding box dynamically based on canvas dimensions, avoiding 'new' keyword
		saveButtonRect.set(centerX - BUTTON_WIDTH / 2, centerY - BUTTON_HEIGHT / 2, centerX + BUTTON_WIDTH / 2, centerY + BUTTON_HEIGHT / 2);
		canvas.drawRect(saveButtonRect, pButton);

		// Dynamic text scaling to fit within the predefined button padding
		String text = "SAVE LAYOUT";
		float textWidth = pText.measureText(text);

		if (textWidth + (2 * TEXT_PADDING) > saveButtonRect.width()) {
			float scale = (float) (saveButtonRect.width() - (2 * TEXT_PADDING)) / textWidth;
			pText.setTextSize(pText.getTextSize() * scale);
		}

		float textY = saveButtonRect.centerY() - ((pText.descent() + pText.ascent()) / 2);
		canvas.drawText(text, saveButtonRect.centerX(), textY, pText);

		// Render the semi-transparent hitboxes over the interactive controls
		ArrayList<InputValue> ids = mm.getInputHandler().getTouchController().getAllInputData();

		for (InputValue v : ids) {
			Rect r = v.getRect();
			if (r != null) {
				boolean draw = false;
				int type = v.getType();
				int controllerType = mm.getPrefsHelper().getControllerType();

				if (type == TouchController.TYPE_BUTTON_RECT) {
					draw = true;
				} else if (controllerType == PrefsHelper.PREF_DIGITAL_DPAD && type == TouchController.TYPE_STICK_RECT) {
					draw = true;
				} else if (controllerType != PrefsHelper.PREF_DIGITAL_DPAD && type == TouchController.TYPE_ANALOG_RECT) {
					draw = true;
				}

				if (draw) {
					canvas.drawRect(r, pRect);
				}
			}
		}
	}
}
