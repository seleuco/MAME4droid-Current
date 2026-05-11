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

import android.graphics.Color;
import android.view.MotionEvent;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.widgets.WarnWidget;

public class Mouse implements IController {

	protected boolean isMouseEnabled = false;

	public boolean isEnabled() {
		return isMouseEnabled;
	}

	protected MAME4droid mm = null;

	public void setMAME4droid(MAME4droid value) {
		mm = value;
	}

	public boolean handleMouse(MotionEvent event) {

		// Display OSD feedback only on the first hardware mouse interaction
		if (!isMouseEnabled) {
			isMouseEnabled = true;
			CharSequence text = "Mouse is enabled!";
			new WarnWidget.WarnWidgetHelper(mm, text.toString(), 3, Color.GREEN, true);

			mm.getMainHelper().updateMAME4droid();
			mm.getInputHandler().resetInput(true);
		}

		int aBtn = event.getActionButton();
		int actionMasked = event.getActionMasked();

		// --- RELATIVE MOUSE MOVEMENT (Captured Pointer API) ---
		if (actionMasked == MotionEvent.ACTION_MOVE) {

			float dx = event.getX();
			float dy = event.getY();

			// CRITICAL: Android batches high-frequency hardware events (e.g. 1000Hz gaming mice)
			// per UI frame. We MUST accumulate historical relative deltas, otherwise we lose
			// sub-frame precision and the mouse movement will feel stuttery.
			int historySize = event.getHistorySize();
			for (int i = 0; i < historySize; i++) {
				dx += event.getHistoricalX(i);
				dy += event.getHistoricalY(i);
			}

			Emulator.setMouseData(0, Emulator.MOUSE_MOVE, 0, dx, dy);
		}

		// --- HARDWARE BUTTON ROUTING ---
		if (aBtn == MotionEvent.BUTTON_PRIMARY) {
			Emulator.setMouseData(0, actionMasked == MotionEvent.ACTION_BUTTON_PRESS ? Emulator.MOUSE_BTN_DOWN : Emulator.MOUSE_BTN_UP, 1, -1, -1);
		}
		else if (aBtn == MotionEvent.BUTTON_SECONDARY || aBtn == MotionEvent.BUTTON_BACK) {
			Emulator.setMouseData(0, actionMasked == MotionEvent.ACTION_BUTTON_PRESS ? Emulator.MOUSE_BTN_DOWN : Emulator.MOUSE_BTN_UP, 2, -1, -1);
		}
		else if (aBtn == MotionEvent.BUTTON_TERTIARY || aBtn == MotionEvent.BUTTON_FORWARD) {
			Emulator.setMouseData(0, actionMasked == MotionEvent.ACTION_BUTTON_PRESS ? Emulator.MOUSE_BTN_DOWN : Emulator.MOUSE_BTN_UP, 3, -1, -1);
		}

		return true;
	}
}
