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

import android.os.Handler;
import android.os.Looper;
import android.view.MotionEvent;
import android.view.View;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;

public class TouchMouse implements IController {

	protected int mouse_pid = -1;
	protected float mouse_prev_x =  0;
	protected float mouse_prev_y =  0;
	protected float mouse_init_x =  0;
	protected float mouse_init_y =  0;
	protected long mouse_millis =  0;

	protected boolean mouse_btn1_pressed = false;
	protected boolean mouse_btn2_pressed = false;
	protected boolean mouse_btn1_cancelled = false;
	protected boolean mouse_btn1_pending = false;

	protected MAME4droid mm = null;

	// Single async handler to manage input delays.
	private final Handler mHandler = new Handler(Looper.getMainLooper());

	public void setMAME4droid(MAME4droid value) {
		mm = value;
	}

	public int getMousePid() {
		return mouse_pid;
	}

	public void handleTouchMouse(View v, MotionEvent event) {
		int action = event.getAction();
		int actionEvent = action & MotionEvent.ACTION_MASK;

		int pointerIndex = (action & MotionEvent.ACTION_POINTER_INDEX_MASK) >> MotionEvent.ACTION_POINTER_INDEX_SHIFT;
		int pid = event.getPointerId(pointerIndex);

		if (actionEvent == MotionEvent.ACTION_UP ||
			actionEvent == MotionEvent.ACTION_POINTER_UP ||
			actionEvent == MotionEvent.ACTION_CANCEL) {

			if (pid == mouse_pid) {
				mouse_pid = -1;

				float cx = mouse_prev_x - mouse_init_x;
				float cy = mouse_prev_y - mouse_init_y;

				// DPI SCALING: 4dp is the standard threshold to absorb natural finger jitter
				// during a quick tap without accidentally triggering a mouse move event.
				float density = (mm != null) ? mm.getResources().getDisplayMetrics().density : 1.0f;
				float tapTolerance = 4.0f * density;

				// Fast left-click (Tap) detection
				if (System.currentTimeMillis() - mouse_millis < 250 && Math.abs(cx) <= tapTolerance && Math.abs(cy) <= tapTolerance) {
					Emulator.setMouseData(0, Emulator.MOUSE_BTN_DOWN, 1, -1, -1);

					// Release the click asynchronously after 60ms to ensure the native engine registers it
					mHandler.postDelayed(new Runnable() {
						@Override
						public void run() {
							Emulator.setMouseData(0, Emulator.MOUSE_BTN_UP, 1, -1, -1);
						}
					}, 60);
				}

				mouse_prev_x = 0;
				mouse_prev_y = 0;
			}

			// Multi-finger click release handling (2 fingers = Left Click, 3 fingers = Right Click)
			if ((mouse_btn1_pending || mouse_btn1_pressed) && event.getPointerCount() <= 2) {
				// Delay the UP event slightly to ensure the emulator engine catches the DOWN event first
				mHandler.postDelayed(new Runnable() {
					@Override
					public void run() {
						if (mouse_btn1_pressed) {
							Emulator.setMouseData(0, Emulator.MOUSE_BTN_UP, 1, -1, -1);
							mouse_btn1_pressed = false;
						}
					}
				}, 30);
			}

			if (mouse_btn2_pressed) {
				Emulator.setMouseData(0, Emulator.MOUSE_BTN_UP, 2, -1, -1);
				mouse_btn2_pressed = false;
			}

		} else { // MOVE / DOWN / POINTER_DOWN

			if (mouse_pid != -1 && actionEvent == MotionEvent.ACTION_POINTER_DOWN) {
				int numpointers = event.getPointerCount();
				long elapsed = System.currentTimeMillis() - mouse_millis;

				if (numpointers == 2 && !mouse_btn1_pending && !mouse_btn1_pressed && elapsed > 150) {
					mouse_btn1_cancelled = false;
					mouse_btn1_pending = true;

					// 25ms grace period to check if a 3rd finger lands (Right click override)
					mHandler.postDelayed(new Runnable() {
						@Override
						public void run() {
							mouse_btn1_pressed = !mouse_btn1_cancelled;
							if (mouse_btn1_pressed) {
								Emulator.setMouseData(0, Emulator.MOUSE_BTN_DOWN, 1, -1, -1);
							}
							mouse_btn1_pending = false;
						}
					}, 25);

				} else if (numpointers == 3 && !mouse_btn2_pressed) {
					// 3-finger right click detected. Cancel the pending left click and fire right click.
					mouse_btn1_cancelled = true;
					mouse_btn2_pressed = true;
					Emulator.setMouseData(0, Emulator.MOUSE_BTN_DOWN, 2, -1, -1);
				}
			}

			for (int i = 0; i < event.getPointerCount(); i++) {
				int pointerId = event.getPointerId(i);
				float x = event.getX(i);
				float y = event.getY(i);

				if (mouse_pid == -1) {
					// Anchor the first finger as the primary mouse pointer
					mouse_pid = pointerId;
					mouse_init_x = x;
					mouse_init_y = y;
					mouse_prev_x = x;
					mouse_prev_y = y;
					mouse_millis = System.currentTimeMillis();
				} else if (mouse_pid == pointerId) {
					// Calculate relative mouse delta
					float cx = x - mouse_prev_x;
					mouse_prev_x = x;

					float cy = y - mouse_prev_y;
					mouse_prev_y = y;

					Emulator.setMouseData(0, Emulator.MOUSE_MOVE, 0, cx, cy);
				}
			}
		}
	}
}
