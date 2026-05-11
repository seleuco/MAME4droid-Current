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

	protected long millis_pressed = 0;
	protected boolean press_on = false;

	protected MAME4droid mm = null;

	public void setMAME4droid(MAME4droid value) {
		mm = value;
	}

	public int getLightgun_pid(){
		return lightgun_pid;
	}

	public void reset() {
		lightgun_pid = -1;
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
				millis_pressed = 0;
				press_on = false;
				lightgun_pid = -1;
				digital_data[0] &= ~A_VALUE;
				digital_data[0] &= ~B_VALUE;
			} else {
				// Secondary touch released
				if (!press_on) {
					digital_data[0] &= ~B_VALUE;
				} else {
					digital_data[0] &= ~A_VALUE;
				}
			}

			Emulator.setDigitalData(0, digital_data[0]);

		} else { // DOWN or MOVE events

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

						// Map absolute screen coordinates to MAME's analog range [-1.0, 1.0]
						float xf = (float) (x - (viewWidth / 2)) / (viewWidth / 2);
						float yf = (float) (y - (viewHeight / 2)) / (viewHeight / 2);

						// Clamp core values to prevent sending invalid out-of-bounds data to the emulator
						xf = Math.max(-1.0f, Math.min(1.0f, xf));
						yf = Math.max(-1.0f, Math.min(1.0f, yf));

						// Anchor the primary touch to the Lightgun reticle
						if (lightgun_pid == -1) {
							lightgun_pid = pointerId;
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

								// Fire main trigger
								if ((digital_data[0] & B_VALUE) == 0) {
									digital_data[0] |= A_VALUE;
								}

								// Long-press detection to swap to alternate fire (e.g. Machine Gun / Grenade)
								if (mm.getPrefsHelper().isLightgunLongPress()) {
									int wait = (mm.getMainHelper().getDeviceDetected() == MainHelper.DEVICE_METAQUEST) ? 300 : 125;

									if (millis_pressed == 0) {
										millis_pressed = System.currentTimeMillis();
									} else if (System.currentTimeMillis() - millis_pressed > wait && !press_on) {
										press_on = true;
										digital_data[0] |= B_VALUE;
										digital_data[0] &= ~A_VALUE;
									}
								}
							}
						} else {
							// SECONDARY TOUCH LOGIC (Multi-finger support)
							if (!press_on) {
								digital_data[0] &= ~A_VALUE;
								digital_data[0] |= B_VALUE; // Engage secondary button (Reload/Cover)
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
			Emulator.setDigitalData(0, digital_data[0]);
		}
	}
}
