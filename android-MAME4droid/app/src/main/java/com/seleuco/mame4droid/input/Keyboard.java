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

import static android.content.res.Configuration.KEYBOARD_QWERTY;

import android.graphics.Color;
import android.view.InputDevice;
import android.view.KeyEvent;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.widgets.WarnWidget;

public class Keyboard implements IController {

	// State trackers for the software keyboard debounce hack
	protected long last_soft_key_time = -1;
	protected long last_soft_key_code = -1;

	protected boolean isKeyboardEnabled = false;

	public boolean isKeyboardEnabled() { return isKeyboardEnabled; }

	protected MAME4droid mm = null;

	public void setMAME4droid(MAME4droid value) {
		mm = value;
	}

	public boolean isKeyboardConnected() {
		// Checks if a physical hardware keyboard is currently attached to the Android device
		return mm.getResources().getConfiguration().keyboard == KEYBOARD_QWERTY && mm.getPrefsHelper().isKeyboardEnabled();
	}

	public boolean handleKeyboard(int keyCode, KeyEvent event){
		InputDevice device = event.getDevice();

		// Failsafe: Null devices are treated as virtual/injected inputs (e.g. on-screen keyboards)
		boolean isVirtual = (device == null) || device.isVirtual();

		if(isVirtual && !mm.getPrefsHelper().isVirtualKeyboardEnabled())
			return false;

		// --- VIRTUAL KEYBOARD DEBOUNCE LOGIC ---
		// Native MAME engine might drop keypresses if the UP event arrives too fast
		// (under ~45ms or ~3 frames). We enforce a minimum hold time for virtual keys.
		if(isVirtual) {
			final int wait_time = 45;

			if(event.getAction() == KeyEvent.ACTION_DOWN && last_soft_key_time == -1) {
				last_soft_key_time = event.getEventTime();
				last_soft_key_code = event.getKeyCode();
			}
			else if(last_soft_key_code == event.getKeyCode() && event.getAction() == KeyEvent.ACTION_UP) {

				long timeElapsed = event.getEventTime() - last_soft_key_time;

				if(timeElapsed < wait_time) {
					final int finalKeyCode = event.getKeyCode();
					final char finalUnicode = (char) event.getUnicodeChar();

					// Never use Thread.sleep() here, it freezes the UI thread and tanks FPS.
					// Instead, use a Handler to dispatch the KEY_UP event asynchronously in the future.
					new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(new Runnable() {
						@Override
						public void run() {
							Emulator.setKeyData(finalKeyCode, Emulator.KEY_UP, finalUnicode);
						}
					}, wait_time - timeElapsed);

					last_soft_key_time = -1;
					return true; // Consume the original event since we scheduled its future release
				}

				last_soft_key_time = -1;
			}
		}

		boolean handle_keyboard = isVirtual || this.isKeyboardConnected();
		int res = 0;

		// Route raw key data directly to the native MAME core
		if(handle_keyboard) {
			res = Emulator.setKeyData(event.getKeyCode(),
				event.getAction() == KeyEvent.ACTION_DOWN ? Emulator.KEY_DOWN : Emulator.KEY_UP,
				(char) event.getUnicodeChar());
		}

		boolean handled = res == 1;

		if(handled) {
			// Trigger OSD feedback only on the first valid physical keyboard press
			if(!isVirtual && !isKeyboardEnabled) {
				isKeyboardEnabled = true;
				CharSequence text = mm.getString(com.seleuco.mame4droid.R.string.keyboard_enabled);
				new WarnWidget.WarnWidgetHelper(mm, text.toString(), 3, Color.GREEN, true);

				mm.getMainHelper().updateMAME4droid();
				mm.getInputHandler().resetInput(true);
			}
		}

		return handled;
	}
}
