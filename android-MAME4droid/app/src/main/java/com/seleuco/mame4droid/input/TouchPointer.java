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

import android.view.MotionEvent;
import android.view.View;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;

public class TouchPointer implements IController {

	protected int touchpointer_pid = -1;
	protected boolean touchpointer_down = false;

	protected MAME4droid mm = null;

	public void setMAME4droid(MAME4droid value) {
		mm = value;
	}

	public int getTouchPointerPid() {
		return touchpointer_pid;
	}

	protected float getCX(int pointerId, View v, MotionEvent event) {
		int i = event.findPointerIndex(pointerId);

		// Failsafe: Android can occasionally drop pointer indices in rapid multi-touch scenarios
		if (i == -1) return -1;

		final int[] location = {0, 0};
		v.getLocationOnScreen(location);
		int x = (int) event.getX(i) + location[0];

		if (mm.getEmuView() != null) {
			mm.getEmuView().getLocationOnScreen(location);
			x -= location[0];

			float viewWidth = mm.getEmuView().getWidth();
			float emuWidth = Emulator.getEmulatedWidth();

			// Protect against division by zero during screen rotations or layout passes
			if (viewWidth > 0) {
				// Optimized math: Translates absolute screen touch to emulated coordinate space
				float xf = ((float) x / viewWidth) * emuWidth;

				// Clamp to screen bounds to prevent sending out-of-bounds coordinates to the core
				return Math.max(0, Math.min(emuWidth, xf));
			}
		}
		return -1;
	}

	protected float getCY(int pointerId, View v, MotionEvent event) {
		int i = event.findPointerIndex(pointerId);

		if (i == -1) return -1;

		final int[] location = {0, 0};
		v.getLocationOnScreen(location);
		int y = (int) event.getY(i) + location[1];

		if (mm.getEmuView() != null) {
			mm.getEmuView().getLocationOnScreen(location);
			y -= location[1];

			float viewHeight = mm.getEmuView().getHeight();
			float emuHeight = Emulator.getEmulatedHeight();

			if (viewHeight > 0) {
				float yf = ((float) y / viewHeight) * emuHeight;
				return Math.max(0, Math.min(emuHeight, yf));
			}
		}
		return -1;
	}

	public void handleTouchPointer(View v, MotionEvent event) {
		int action = event.getAction();
		int actionEvent = action & MotionEvent.ACTION_MASK;

		int pointerIndex = (action & MotionEvent.ACTION_POINTER_INDEX_MASK) >> MotionEvent.ACTION_POINTER_INDEX_SHIFT;
		int pid = event.getPointerId(pointerIndex);

		if (actionEvent == MotionEvent.ACTION_UP ||
			actionEvent == MotionEvent.ACTION_POINTER_UP ||
			actionEvent == MotionEvent.ACTION_CANCEL) {

			if (pid == touchpointer_pid) {
				if (touchpointer_down) {
					float xf = this.getCX(touchpointer_pid, v, event);
					float yf = this.getCY(touchpointer_pid, v, event);

					// Send the release event. If coordinates were lost during UP, fallback to -1.
					if (xf != -1 && yf != -1) {
						Emulator.setTouchData(0, Emulator.FINGER_UP, xf, yf);
					} else {
						Emulator.setTouchData(0, Emulator.FINGER_UP, -1, -1);
					}
					touchpointer_down = false;
				}
				touchpointer_pid = -1;
			}
		} else { // MOVE / DOWN / POINTER_DOWN

			for (int i = 0; i < event.getPointerCount(); i++) {
				int pointerId = event.getPointerId(i);

				// Anchor the primary touch to the emulation pointer
				if (touchpointer_pid == -1) {
					touchpointer_pid = pointerId;
				}

				if (touchpointer_pid == pointerId) {
					float xf = this.getCX(touchpointer_pid, v, event);
					float yf = this.getCY(touchpointer_pid, v, event);

					// Skip invalid or dropped pointer coordinates
					if (xf == -1 || yf == -1) continue;

					Emulator.setTouchData(0, Emulator.FINGER_MOVE, xf, yf);

					// Fire the initial DOWN state if not already engaged (and tilt isn't overriding)
					if (!touchpointer_down && !mm.getInputHandler().getTiltSensor().isEnabled()) {
						Emulator.setTouchData(0, Emulator.FINGER_DOWN, xf, yf);
						touchpointer_down = true;
					}
				}
			}
		}
	}
}
