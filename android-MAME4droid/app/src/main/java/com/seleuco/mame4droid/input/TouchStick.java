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

import android.content.res.Configuration;
import android.graphics.Canvas;
import android.graphics.Point;
import android.graphics.Rect;
import android.graphics.drawable.BitmapDrawable;
import android.view.MotionEvent;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.R;
import com.seleuco.mame4droid.helpers.PrefsHelper;

public class TouchStick implements IController {

	float MY_PI = 3.14159265f;

	Rect rStickArea = new Rect();
	Rect stickPos = new Rect();

	Point ptCur = new Point();
	Point ptCenter = new Point();
	Point ptMin = new Point();
	Point ptMax = new Point();

	int stickWidth;
	int stickHeight;

	float deadZone = 0.1f;

	float ang; // Angle the joystick is being held
	float mag; // Magnitude of the joystick (range 0.0 to 1.0)
	float rx, ry, oldRx, oldRy;

	int motion_pid = -1;

	static BitmapDrawable inner_img = null;
	static BitmapDrawable outer_img = null;
	static BitmapDrawable stick_images[] = null;

	protected MAME4droid mm = null;

	final public float rad2degree(float r) {
		return ((r * 180.0f) / MY_PI);
	}

	public void setMAME4droid(MAME4droid value) {
		mm = value;
		if (mm == null) return;

		// Cache drawable resources globally to prevent allocations during render loops
		if (inner_img == null) inner_img = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.stick_inner);
		if (outer_img == null) outer_img = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.stick_outer);

		if (stick_images == null) {
			stick_images = new BitmapDrawable[9];
			stick_images[IController.STICK_DOWN] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.stick_down);
			stick_images[IController.STICK_DOWN_LEFT] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.stick_down_left);
			stick_images[IController.STICK_DOWN_RIGHT] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.stick_down_right);
			stick_images[IController.STICK_LEFT] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.stick_left);
			stick_images[IController.STICK_NONE] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.stick_none);
			stick_images[IController.STICK_RIGHT] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.stick_right);
			stick_images[IController.STICK_UP] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.stick_up);
			stick_images[IController.STICK_UP_LEFT] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.stick_up_left);
			stick_images[IController.STICK_UP_RIGHT] = (BitmapDrawable) mm.getResources().getDrawable(R.drawable.stick_up_right);
		}
	}

	public int getMotionPid() {
		return motion_pid;
	}

	public void reset() {
		if (motion_pid != -1) {
			ptCur.x = ptCenter.x;
			ptCur.y = ptCenter.y;
			rx = ry = mag = 0;
			oldRx = oldRy = -999;
			motion_pid = -1;
		}
	}

	public void setStickArea(Rect rStickArea) {
		this.rStickArea = rStickArea;
		ptMin.x = rStickArea.left;
		ptMin.y = rStickArea.top;
		ptMax.x = rStickArea.right;
		ptMax.y = rStickArea.bottom;
		ptCenter.x = rStickArea.centerX();
		ptCenter.y = rStickArea.centerY();

		// Size the visual stick components relative to the bounded area
		stickWidth = (int) ((float) rStickArea.width() * 0.62f);
		stickHeight = (int) ((float) rStickArea.height() * 0.62f);

		calculateStickPosition(ptCenter);
	}

	protected int updateAnalog(int pad_data) {
		switch (mm.getPrefsHelper().getAnalogDZ()) {
			case 1: deadZone = 0.01f; break;
			case 2: deadZone = 0.1f; break;
			case 3: deadZone = 0.15f; break;
			case 4: deadZone = 0.2f; break;
			case 5: deadZone = 0.3f; break;
		}

		if (mag >= deadZone) {
			int ways = mm.getPrefsHelper().getStickWays();
			if (ways == -1) ways = Emulator.getValue(Emulator.NUMWAYS);
			boolean inGame = Emulator.isInGameButNotInMenu();

			// Direct Analog Routing
			if (mm.getPrefsHelper().getControllerType() != PrefsHelper.PREF_DIGITAL_STICK) {
				// Invert Y axis to match MAME's native coordinate system
				Emulator.setAnalogData(Emulator.LEFT_STICK_DATA, 0, rx, ry * -1.0f);
				return pad_data; // Pure analog handling bypasses digital state flags
			}

			float v = ang;

			// Strict Digital Way-Mapping
			if (ways == 2 && inGame) {
				if (v < 180) {
					pad_data |= RIGHT_VALUE;
					pad_data &= ~(UP_VALUE | DOWN_VALUE | LEFT_VALUE);
				} else if (v >= 180) {
					pad_data |= LEFT_VALUE;
					pad_data &= ~(UP_VALUE | DOWN_VALUE | RIGHT_VALUE);
				}
			} else if (ways == 4 || !inGame) {
				if (v >= 315 || v < 45) {
					pad_data |= DOWN_VALUE;
					pad_data &= ~(UP_VALUE | LEFT_VALUE | RIGHT_VALUE);
				} else if (v >= 45 && v < 135) {
					pad_data |= RIGHT_VALUE;
					pad_data &= ~(UP_VALUE | DOWN_VALUE | LEFT_VALUE);
				} else if (v >= 135 && v < 225) {
					pad_data |= UP_VALUE;
					pad_data &= ~(DOWN_VALUE | LEFT_VALUE | RIGHT_VALUE);
				} else if (v >= 225 && v < 315) {
					pad_data |= LEFT_VALUE;
					pad_data &= ~(UP_VALUE | DOWN_VALUE | RIGHT_VALUE);
				}
			} else { // 8-Way Movement
				if (v >= 330 || v < 30) {
					pad_data |= DOWN_VALUE;
					pad_data &= ~(UP_VALUE | LEFT_VALUE | RIGHT_VALUE);
				} else if (v >= 30 && v < 60) {
					pad_data |= (DOWN_VALUE | RIGHT_VALUE);
					pad_data &= ~(UP_VALUE | LEFT_VALUE);
				} else if (v >= 60 && v < 120) {
					pad_data |= RIGHT_VALUE;
					pad_data &= ~(UP_VALUE | DOWN_VALUE | LEFT_VALUE);
				} else if (v >= 120 && v < 150) {
					pad_data |= (RIGHT_VALUE | UP_VALUE);
					pad_data &= ~(DOWN_VALUE | LEFT_VALUE);
				} else if (v >= 150 && v < 210) {
					pad_data |= UP_VALUE;
					pad_data &= ~(DOWN_VALUE | LEFT_VALUE | RIGHT_VALUE);
				} else if (v >= 210 && v < 240) {
					pad_data |= (UP_VALUE | LEFT_VALUE);
					pad_data &= ~(DOWN_VALUE | RIGHT_VALUE);
				} else if (v >= 240 && v < 300) {
					pad_data |= LEFT_VALUE;
					pad_data &= ~(UP_VALUE | DOWN_VALUE | RIGHT_VALUE);
				} else if (v >= 300 && v < 330) {
					pad_data |= (LEFT_VALUE | DOWN_VALUE);
					pad_data &= ~(UP_VALUE | RIGHT_VALUE);
				}
			}
		} else {
			// Apply deadzone center
			Emulator.setAnalogData(Emulator.LEFT_STICK_DATA, 0, 0.0f, 0.0f);
			pad_data &= ~(UP_VALUE | DOWN_VALUE | LEFT_VALUE | RIGHT_VALUE);
		}

		return pad_data;
	}

	protected void calculateStickState(Point pt, Point min, Point max, Point center) {
		// Enforce boundary constraints
		if (pt.x > max.x) pt.x = max.x;
		if (pt.x < min.x) pt.x = min.x;
		if (pt.y > max.y) pt.y = max.y;
		if (pt.y < min.y) pt.y = min.y;

		// Calculate normalized vectors (-1.0 to 1.0)
		rx = (pt.x == center.x) ? 0 :
			(pt.x >= center.x) ? ((float) (pt.x - center.x) / (float) (max.x - center.x)) :
				((float) (pt.x - min.x) / (float) (center.x - min.x)) - 1.0f;

		ry = (pt.y == center.y) ? 0 :
			(pt.y >= center.y) ? ((float) (pt.y - center.y) / (float) (max.y - center.y)) :
				((float) (pt.y - min.y) / (float) (center.y - min.y)) - 1.0f;

		// Mathematical safety: Prevent 0/0 yielding NaN which corrupts positional tracking
		if (rx == 0.0f && ry == 0.0f) {
			ang = 0;
			mag = 0;
			return;
		}

		/* Calculate the joystick angle and magnitude */
		ang = rad2degree((float) Math.atan(ry / rx));

		// MAME4droid specific compass adjustment
		ang -= 90.0f;
		if (rx < 0.0f) ang -= 180.0f;
		ang = Math.abs(ang);

		mag = (float) Math.sqrt((rx * rx) + (ry * ry));

		// Circular clamping for analog precision
		if (mag > 1.0f) {
			rx = rx / mag;
			ry = ry / mag;
			mag = 1.0f;
			// Recalculate physical point based on clamped vectors
			pt.x = ptCenter.x + (int)(rx * (ptMax.x - ptCenter.x));
			pt.y = ptCenter.y + (int)(ry * (ptMax.y - ptCenter.y));
		}
	}

	protected void calculateStickPosition(Point pt) {
		int ways = mm.getPrefsHelper().getStickWays();
		if (ways == -1) ways = Emulator.getValue(Emulator.NUMWAYS);
		boolean inGame = Emulator.isInGameButNotInMenu();

		// Visually constrain the inner thumb pad based on the current way-restriction logic
		if (ways == 2 && inGame) {
			stickPos.left = Math.min(ptMax.x - stickWidth, Math.max(ptMin.x, pt.x - (stickWidth / 2)));
			stickPos.top = ptCenter.y - (stickHeight / 2);
		} else if ((ways == 4 || !inGame) && mm.getPrefsHelper().getControllerType() == PrefsHelper.PREF_DIGITAL_STICK) {
			int state = mm.getInputHandler().getTouchController().getStick_state();
			if (state == STICK_RIGHT || state == STICK_LEFT) {
				stickPos.left = Math.min(ptMax.x - stickWidth, Math.max(ptMin.x, pt.x - (stickWidth / 2)));
				stickPos.top = ptCenter.y - (stickHeight / 2);
			} else {
				stickPos.left = ptCenter.x - (stickWidth / 2);
				stickPos.top = Math.min(ptMax.y - stickHeight, Math.max(ptMin.y, pt.y - (stickHeight / 2)));
			}
		} else {
			stickPos.left = Math.min(ptMax.x - stickWidth, Math.max(ptMin.x, pt.x - (stickWidth / 2)));
			stickPos.top = Math.min(ptMax.y - stickHeight, Math.max(ptMin.y, pt.y - (stickHeight / 2)));
		}

		stickPos.right = stickPos.left + stickWidth;
		stickPos.bottom = stickPos.top + stickHeight;
	}

	public int handleMotion(MotionEvent event, int pad_data) {
		int actionMasked = event.getActionMasked();
		int pointerIndex = event.getActionIndex();
		int pid = event.getPointerId(pointerIndex);

		if (actionMasked == MotionEvent.ACTION_UP ||
			(actionMasked == MotionEvent.ACTION_POINTER_UP && pid == motion_pid) ||
			actionMasked == MotionEvent.ACTION_CANCEL) {
			reset();
		} else {
			for (int i = 0; i < event.getPointerCount(); i++) {
				int pointerId = event.getPointerId(i);
				int x = (int) event.getX(i);
				int y = (int) event.getY(i);

				// Anchor the stick strictly to the first finger that engages it.
				// Ensures overlapping secondary touches (e.g. multi-tap shooting) don't steal focus.
				//TODO: revisar si hayq e comentar motion_pid == -1 no sea que interfiera con otro control
				if (rStickArea.contains(x, y) && motion_pid == -1) {
					motion_pid = pointerId;
				}

				if (motion_pid == pointerId) {
					ptCur.x = x;
					ptCur.y = y;
					calculateStickState(ptCur, ptMin, ptMax, ptCenter);
				}
			}
		}

		pad_data = updateAnalog(pad_data);

		// Limit UI invalidations by only redrawing when significant deltas occur
		double inc = mm.getPrefsHelper().isDebugEnabled() ? 0.01 : 0.08;

		if ((Math.abs(oldRx - rx) >= inc || Math.abs(oldRy - ry) >= inc) && mm.getPrefsHelper().isAnimatedInput()) {
			oldRx = rx;
			oldRy = ry;

			calculateStickPosition((mag >= deadZone ? ptCur : ptCenter));

			if (mm.getPrefsHelper().getControllerType() == PrefsHelper.PREF_ANALOG_STICK) {
				if (Emulator.isDebug()) mm.getInputView().invalidate();
				else mm.getInputView().invalidate(rStickArea);
			}
		}

		return pad_data;
	}

	public void draw(Canvas canvas) {
		TiltSensor tiltSensor = mm.getInputHandler().getTiltSensor();
		int orientation = mm.getMainHelper().getscrOrientation();

		if (mm.getPrefsHelper().getControllerType() == PrefsHelper.PREF_ANALOG_STICK && !tiltSensor.isEnabled()) {

			if (orientation == Configuration.ORIENTATION_LANDSCAPE ||
				(orientation == Configuration.ORIENTATION_PORTRAIT && Emulator.isPortraitFull())) {
				outer_img.setBounds(rStickArea);
				outer_img.setAlpha(mm.getMainHelper().getControllerAlpha());
				outer_img.draw(canvas);
			}

			inner_img.setBounds(stickPos);
			inner_img.setAlpha(mm.getMainHelper().getControllerAlpha());
			inner_img.draw(canvas);

		} else if (mm.getPrefsHelper().getControllerType() == PrefsHelper.PREF_DIGITAL_STICK ||
			(mm.getPrefsHelper().getControllerType() == PrefsHelper.PREF_ANALOG_STICK && tiltSensor.isEnabled())) {

			int currentState = mm.getInputHandler().getTouchController().getStick_state();
			stick_images[currentState].setBounds(rStickArea);
			stick_images[currentState].setAlpha(mm.getMainHelper().getControllerAlpha());
			stick_images[currentState].draw(canvas);
		}

		if (Emulator.isDebug()) {
			canvas.drawText("x=" + ptCur.x + " y=" + ptCur.y + " state=" +
					mm.getInputHandler().getTouchController().getStick_state() +
					" rx=" + rx + " ry=" + ry + " ang=" + ang + " mag=" + mag,
				5, 50, Emulator.getDebugPaint());
		}
	}
}
