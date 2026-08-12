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

import android.util.Log;
import android.view.MotionEvent;
import android.view.View;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.helpers.MainHelper;

public class TouchLightgun implements IController {

	protected int lightgun_pid = -1;

	protected boolean press_on = false;

	//pointer holding cover with a second finger, -1 if none. Tells our own B
	//apart from a pedal held on a physical or virtual button.
	protected int cover_pid = -1;

	//game screen inside the view, normalized. Whole view when there is no artwork.
	private float screen_x0 = 0.0f, screen_y0 = 0.0f, screen_w = 1.0f, screen_h = 1.0f;

	protected MAME4droid mm = null;

	//long press has to run on a timer: a still finger sends no MOVE events, so
	//checking the elapsed time inside the handler depends on the touch rate.
	private final android.os.Handler longPressHandler =
		new android.os.Handler(android.os.Looper.getMainLooper());
	private int[] digital_data_ref = null;

	private final Runnable longPressRunnable = new Runnable() {
		public void run() {
			if (lightgun_pid == -1 || press_on || digital_data_ref == null) return;
			press_on = true;
			digital_data_ref[0] |= B_VALUE;
			digital_data_ref[0] &= ~A_VALUE;
			Emulator.setDigitalData(0, digital_data_ref[0]);
		}
	};

	private void cancelLongPress() {
		longPressHandler.removeCallbacks(longPressRunnable);
	}

	private void refreshScreenRect() {
		try {
			float x0 = Emulator.getValue(Emulator.SCREEN_RECT, 0) / 10000.0f;
			float y0 = Emulator.getValue(Emulator.SCREEN_RECT, 1) / 10000.0f;
			float x1 = Emulator.getValue(Emulator.SCREEN_RECT, 2) / 10000.0f;
			float y1 = Emulator.getValue(Emulator.SCREEN_RECT, 3) / 10000.0f;
			if (x1 - x0 > 0.01f && y1 - y0 > 0.01f) {
				screen_x0 = x0; screen_y0 = y0;
				screen_w = x1 - x0; screen_h = y1 - y0;
			}
		} catch (Throwable ignored) {}
	}

	public void setMAME4droid(MAME4droid value) {
		mm = value;
	}

	public int getLightgun_pid(){
		return lightgun_pid;
	}

	public void reset() {
		cancelLongPress();
		lightgun_pid = -1;
		cover_pid = -1;
		press_on = false;
	}

	public void handleTouchLightgun(View v, MotionEvent event, int[] digital_data) {
		int action = event.getAction();
		int actionEvent = action & MotionEvent.ACTION_MASK;

		int pointerIndex = (action & MotionEvent.ACTION_POINTER_INDEX_MASK) >> MotionEvent.ACTION_POINTER_INDEX_SHIFT;
		int pid = event.getPointerId(pointerIndex);

		if (actionEvent == MotionEvent.ACTION_UP ||
			actionEvent == MotionEvent.ACTION_POINTER_UP ||
			actionEvent == MotionEvent.ACTION_CANCEL) {

			if (pid == lightgun_pid) {
				// Primary trigger released
				cancelLongPress();
				boolean cover_was_ours = press_on || cover_pid != -1;
				press_on = false;
				lightgun_pid = -1;
				digital_data[0] &= ~A_VALUE;
				//leave B alone if a pedal button is holding it
				if (cover_was_ours) {
					digital_data[0] &= ~B_VALUE;
					cover_pid = -1;
				}
			} else {
				// Secondary touch released
				if (!press_on) {
					if (pid == cover_pid) {
						cover_pid = -1;
						digital_data[0] &= ~B_VALUE;
					}
				} else {
					digital_data[0] &= ~A_VALUE;
				}
			}

			Emulator.setDigitalData(0, digital_data[0]);

		} else { // DOWN or MOVE events

			refreshScreenRect();

			// Snapshot button state to vibrate only on new engagements below
			int oldButtons = digital_data[0] & (A_VALUE | B_VALUE);

			// Allocate location array ONCE outside the loop to prevent Garbage Collector churn
			// and avoid dropping frames during rapid continuous touch events.
			final int[] location = new int[2];

			for (int i = 0; i < event.getPointerCount(); i++) {
				int pointerId = event.getPointerId(i);

				if (pointerId == mm.getInputHandler().getTouchStick().getMotionPid()) {
					continue;
				}

				v.getLocationOnScreen(location);
				int x = (int) event.getX(i) + location[0];
				int y = (int) event.getY(i) + location[1];

				if (mm.getEmuView() != null) {
					mm.getEmuView().getLocationOnScreen(location);
					x -= location[0];
					y -= location[1];

					float viewWidth = mm.getEmuView().getWidth();
					float viewHeight = mm.getEmuView().getHeight();

					// Prevent division by zero if layout isn't fully initialized
					if (viewWidth > 0 && viewHeight > 0) {

						// MAME's [-1,1] is the game screen, not the whole canvas: with
						// the full emulation area the screen is a sub-rect of the view.
						// All normalized, so the render resolution doesn't matter.
						float u = x / viewWidth;
						float w = y / viewHeight;

						float xf = ((u - screen_x0) / screen_w) * 2.0f - 1.0f;
						float yf = ((w - screen_y0) / screen_h) * 2.0f - 1.0f;

						// Clamp core values to prevent sending invalid out-of-bounds data to the emulator
						xf = Math.max(-1.0f, Math.min(1.0f, xf));
						yf = Math.max(-1.0f, Math.min(1.0f, yf));

						// Anchor the primary touch to the Lightgun reticle
						if (lightgun_pid == -1) {
							lightgun_pid = pointerId;
							if (mm.getPrefsHelper().isLightgunLongPress()) {
								int wait = (mm.getMainHelper().getDeviceDetected() == MainHelper.DEVICE_METAQUEST) ? 300 : 125;
								digital_data_ref = digital_data;
								cancelLongPress();
								longPressHandler.postDelayed(longPressRunnable, wait);
							}
						}

						if (lightgun_pid == pointerId) {

							// PRIMARY TOUCH LOGIC (Aiming & Trigger)
							if (!press_on) {

								// Hack: Allow yf to exceed bounds specifically for "shoot off-screen to reload" mechanics
								if (mm.getPrefsHelper().isBottomReload() && yf >= 0.85f) {
									yf = 1.1f;
								}

								if (!mm.getInputHandler().getTiltSensor().isEnabled()) {
									// Invert Y axis for native MAME orientation
									Emulator.setAnalogData(Emulator.LIGHTGUN_DATA, 0, xf, -yf);
								}

								// Fire main trigger. Only a second finger holding cover
								// blocks it; a pedal button does not.
								if (cover_pid == -1) {
									digital_data[0] |= A_VALUE;
								}
							}
						} else {
							// SECONDARY TOUCH LOGIC (Multi-finger support)
							if (!press_on) {
								digital_data[0] &= ~A_VALUE;
								digital_data[0] |= B_VALUE; // Engage secondary button (Reload/Cover)
								cover_pid = pointerId;
							} else {
								if (!mm.getInputHandler().getTiltSensor().isEnabled()) {
									Emulator.setAnalogData(Emulator.LIGHTGUN_DATA, 0, xf, -yf);
								}
								digital_data[0] |= A_VALUE;
							}
						}
					}
				}
			}
			// Haptic on press edges only: trigger (button 1) clicks, the
			// secondary button ticks lighter. Releases stay silent.
			int pressed = (digital_data[0] & (A_VALUE | B_VALUE)) & ~oldButtons;
			if (pressed != 0 && mm.getPrefsHelper().isVibrate()) {
				TouchController tc = mm.getInputHandler().getTouchController();
				if ((pressed & A_VALUE) != 0) tc.vibrate();
				else tc.vibrateSecondary();
			}

			Emulator.setDigitalData(0, digital_data[0]);
		}
	}
}
