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

import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Rect;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.util.Log;
import android.view.MotionEvent;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.helpers.DialogHelper;
import com.seleuco.mame4droid.helpers.PrefsHelper;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.StringTokenizer;

public class TouchController implements IController {

	private static final String TAG = "TouchController";

	static final int MAX_FINGERS = 20;

	// State tracking for multi-touch inputs
	protected static int[] newtouches = new int[MAX_FINGERS];
	protected static int[] oldtouches = new int[MAX_FINGERS];
	protected static boolean[] touchstates = new boolean[MAX_FINGERS];

	final public static int TYPE_MAIN_RECT = 1;
	final public static int TYPE_STICK_RECT = 2;
	final public static int TYPE_BUTTON_RECT = 3;
	final public static int TYPE_STICK_IMG = 4;
	final public static int TYPE_BUTTON_IMG = 5;
	final public static int TYPE_SWITCH = 6;
	final public static int TYPE_ALPHA = 7;
	final public static int TYPE_ANALOG_RECT = 8;

	final public static int STATE_SHOWING_CONTROLLER = 1;
	final public static int STATE_SHOWING_NONE = 3;

	protected int state = STATE_SHOWING_CONTROLLER;

	public int getState() {
		return state;
	}

	protected int stick_state;
	public int getStick_state() { return stick_state; }
	protected int old_stick_state;

	protected int[] btnStates = new int[NUM_BUTTONS];
	public int[] getBtnStates() { return btnStates; }
	protected int[] old_btnStates = new int[NUM_BUTTONS];

	protected int ax = 0;
	protected int ay = 0;
	protected float dx = 1;
	protected float dy = 1;

	protected ArrayList<InputValue> values = new ArrayList<>();
	MAME4droid mm = null;

	public TouchController() {
		stick_state = old_stick_state = STICK_NONE;
		for (int i = 0; i < NUM_BUTTONS; i++)
			btnStates[i] = old_btnStates[i] = BTN_NO_PRESS_STATE;
	}

	public void setMAME4droid(MAME4droid value) {
		mm = value;
		if (mm == null) return;

		if (mm.getMainHelper().getscrOrientation() == Configuration.ORIENTATION_LANDSCAPE) {
			state = mm.getPrefsHelper().isLandscapeTouchController() ? STATE_SHOWING_CONTROLLER : STATE_SHOWING_NONE;
		} else {
			state = mm.getPrefsHelper().isPortraitTouchController() ? STATE_SHOWING_CONTROLLER : STATE_SHOWING_NONE;
		}
	}

	public void changeState() {
		if (state == STATE_SHOWING_CONTROLLER) {
			mm.getInputHandler().resetInput(true);
			state = STATE_SHOWING_NONE;
		} else {
			state = STATE_SHOWING_CONTROLLER;
		}
	}

	int getButtonValue(int i, boolean b) {
		switch (i) {
			case 0: return D_VALUE;
			case 1:
				// If B+X macro is enabled, return combined bits. A_VALUE is included to trigger UI animations.
				if (mm.getPrefsHelper().isBplusX() && b) return B_VALUE | A_VALUE | C_VALUE;
				else return C_VALUE;
			case 2: return A_VALUE;
			case 3: return B_VALUE;
			case 4: return E_VALUE;
			case 5: return F_VALUE;
			case 6: return EXIT_VALUE;
			case 7: return OPTION_VALUE;
			case 8: return COIN_VALUE;
			case 9: return START_VALUE;
			case 10: return G_VALUE;
			case 11: return H_VALUE;
		}
		return 0;
	}

	int getStickValue(int i) {
		int ways = mm.getPrefsHelper().getStickWays();
		if (ways == -1) ways = Emulator.getValue(Emulator.NUMWAYS);
		boolean inGame = Emulator.isInGameButNotInMenu();

		if (ways == 2 && inGame) {
			switch (i) {
				case 1: case 4: case 6: return LEFT_VALUE;
				case 3: case 5: case 8: return RIGHT_VALUE;
			}
		} else if (ways == 4 || !inGame) {
			switch (i) {
				case 1: case 4: case 6: return LEFT_VALUE;
				case 2: return UP_VALUE;
				case 3: case 5: case 8: return RIGHT_VALUE;
				case 7: return DOWN_VALUE;
			}
		} else { // 8-way directional
			switch (i) {
				case 1: return UP_VALUE | LEFT_VALUE;
				case 2: return UP_VALUE;
				case 3: return UP_VALUE | RIGHT_VALUE;
				case 4: return LEFT_VALUE;
				case 5: return RIGHT_VALUE;
				case 6: return DOWN_VALUE | LEFT_VALUE;
				case 7: return DOWN_VALUE;
				case 8: return DOWN_VALUE | RIGHT_VALUE;
			}
		}
		return 0;
	}

	public ArrayList<InputValue> getAllInputData() {
		return (state == STATE_SHOWING_CONTROLLER) ? values : null;
	}

	public Rect getMainRect() {
		if (values == null) return null;
		for (int i = 0; i < values.size(); i++) {
			if (values.get(i).getType() == TYPE_MAIN_RECT)
				return values.get(i).getOrigRect();
		}
		return null;
	}

	public boolean handleTouchController(MotionEvent event, int[] digital_data) {
		boolean handled = false;

		// Use modern action masking
		int actionMasked = event.getActionMasked();
		int pointerIndex = event.getActionIndex();
		int pid = event.getPointerId(pointerIndex);

		// Reset state trackers for the current frame iteration
		for (int i = 0; i < MAX_FINGERS; i++) {
			touchstates[i] = false;
			oldtouches[i] = newtouches[i];
		}

		for (int i = 0; i < event.getPointerCount(); i++) {
			int actionPointerId = event.getPointerId(i);

			int x = (int) event.getX(i);
			int y = (int) event.getY(i);

			// Yield priority to the analog TouchStick if it's currently capturing this pointer
			if (actionPointerId == mm.getInputHandler().getTouchStick().getMotionPid()) {
				handled = true;
				continue;
			}

			if (actionMasked == MotionEvent.ACTION_UP
				|| (actionMasked == MotionEvent.ACTION_POINTER_UP && actionPointerId == pid)
				|| actionMasked == MotionEvent.ACTION_CANCEL) {
				// Finger lifted, state will be cleared below
			} else {
				int id = actionPointerId;

				// Safely drop pointers that exceed our tracking pool limit
				if (id >= MAX_FINGERS) continue;

				touchstates[id] = true;

				for (int j = 0; j < values.size(); j++) {
					InputValue iv = values.get(j);

					// AABB Collision check against UI layout rects
					if (iv.getRect().contains(x, y) && isHandledTouchItem(iv)) {

						if (iv.getType() == TYPE_BUTTON_RECT || iv.getType() == TYPE_STICK_RECT) {
							handled = true;

							switch (actionMasked) {
								case MotionEvent.ACTION_DOWN:
								case MotionEvent.ACTION_POINTER_DOWN:
								case MotionEvent.ACTION_MOVE:

									if (iv.getType() == TYPE_BUTTON_RECT) {

										// Prevent cross-talk: Disable Start/Exit while the D-Pad is engaged in portrait
										if ((iv.getValue() == BTN_START || iv.getValue() == BTN_EXIT) && stick_state != STICK_NONE &&
											mm.getMainHelper().getscrOrientation() == Configuration.ORIENTATION_PORTRAIT &&
											!mm.getInputHandler().getTiltSensor().isEnabled()) {
											continue;
										}

										newtouches[id] |= getButtonValue(iv.getValue(), true);

										// System action routing
										if (iv.getValue() == BTN_EXIT && actionMasked != MotionEvent.ACTION_MOVE) {
											Emulator.setValue(Emulator.EXIT_GAME, 1);
											// Execute exit asynchronously to prevent UI freezing
											new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
												@Override
												public void run() {
													Emulator.setValue(Emulator.EXIT_GAME, 0);
												}
											}, InputHandler.PRESS_WAIT);
										} else if (iv.getValue() == BTN_OPTION && actionMasked != MotionEvent.ACTION_MOVE && !Emulator.isInOptions()) {
											Emulator.setInOptions(true);
											mm.showDialog(DialogHelper.DIALOG_OPTIONS);
										}

									} else if (mm.getPrefsHelper().getControllerType() == PrefsHelper.PREF_DIGITAL_DPAD
										&& !((mm.getInputHandler().getTiltSensor().isEnabled() ||
										(mm.getPrefsHelper().isTouchLightgun() && !(mm.getMainHelper().getscrOrientation() == Configuration.ORIENTATION_PORTRAIT && !mm.getPrefsHelper().isPortraitFullscreen())))
										&& Emulator.isInGameButNotInMenu())) {

										// Resolve directional bits based on 2, 4, or 8-way D-Pad configurations
										newtouches[id] = getStickValue(iv.getValue());
									}

									// Commit state differences to the digital payload
									if (oldtouches[id] != newtouches[id]) {
										digital_data[0] &= ~(oldtouches[id]);
									}

									digital_data[0] |= newtouches[id];
							}

							// Exclusive mask for compound buttons
							if (mm.getPrefsHelper().isBplusX() && (iv.getValue() == BTN_A || iv.getValue() == BTN_B))
								break;
						}
					}
				}
			}
		}

		// Reconcile lifting states (Debouncing logic for low-quality digitizers)
		for (int i = 0; i < touchstates.length; i++) {
			if (!touchstates[i] && newtouches[i] != 0) {
				boolean really = true;

				for (int j = 0; j < MAX_FINGERS && really; j++) {
					if (j == i) continue;
					// Verify no other active finger is masking this button press
					really = (newtouches[j] & newtouches[i]) == 0;
				}

				if (really) {
					digital_data[0] &= ~(newtouches[i]);
				}

				newtouches[i] = 0;
				oldtouches[i] = 0;
			}
		}

		handleImageStates(false, digital_data);
		mm.getInputHandler().fixTiltCoin();
		Emulator.setDigitalData(0, digital_data[0]);

		return handled;
	}

	public void handleImageStates(boolean onlyStick, int[] digital_data) {
		PrefsHelper pH = mm.getPrefsHelper();

		if (!pH.isAnimatedInput() && !pH.isVibrate()) return;

		// Extract directional vector
		switch ((int) digital_data[0] & (UP_VALUE | DOWN_VALUE | LEFT_VALUE | RIGHT_VALUE)) {
			case UP_VALUE: stick_state = STICK_UP; break;
			case DOWN_VALUE: stick_state = STICK_DOWN; break;
			case LEFT_VALUE: stick_state = STICK_LEFT; break;
			case RIGHT_VALUE: stick_state = STICK_RIGHT; break;
			case UP_VALUE | LEFT_VALUE: stick_state = STICK_UP_LEFT; break;
			case UP_VALUE | RIGHT_VALUE: stick_state = STICK_UP_RIGHT; break;
			case DOWN_VALUE | LEFT_VALUE: stick_state = STICK_DOWN_LEFT; break;
			case DOWN_VALUE | RIGHT_VALUE: stick_state = STICK_DOWN_RIGHT; break;
			default: stick_state = STICK_NONE;
		}

		// Trigger targeted UI invalidations (repaints) and haptics
		for (int j = 0; j < values.size(); j++) {
			InputValue iv = values.get(j);

			if (iv.getType() == TYPE_STICK_IMG && pH.getControllerType() == PrefsHelper.PREF_DIGITAL_DPAD) {
				if (stick_state != old_stick_state) {
					if (pH.isAnimatedInput()) mm.getInputView().invalidate(iv.getRect());
					if (pH.isVibrate() && stick_state != STICK_NONE) vibrate();
					old_stick_state = stick_state;
				}
			} else if (iv.getType() == TYPE_ANALOG_RECT && pH.getControllerType() != PrefsHelper.PREF_DIGITAL_DPAD) {
				if (stick_state != old_stick_state) {
					if (pH.isAnimatedInput() && (pH.getControllerType() == PrefsHelper.PREF_DIGITAL_STICK ||
						(mm.getPrefsHelper().getControllerType() == PrefsHelper.PREF_ANALOG_STICK && mm.getInputHandler().getTiltSensor().isEnabled()))) {
						if (pH.isDebugEnabled()) mm.getInputView().invalidate();
						else mm.getInputView().invalidate(iv.getRect());
					}
					if (pH.isVibrate() && stick_state != STICK_NONE) vibrate();
					old_stick_state = stick_state;
				}
			} else if (iv.getType() == TYPE_BUTTON_IMG && !onlyStick) {
				int i = iv.getValue();

				btnStates[i] = (digital_data[0] & getButtonValue(i, false)) != 0 ? BTN_PRESS_STATE : BTN_NO_PRESS_STATE;

				if (btnStates[iv.getValue()] != old_btnStates[iv.getValue()]) {
					if (pH.isAnimatedInput()) mm.getInputView().invalidate(iv.getRect());
					if (pH.isVibrate() && btnStates[i] == BTN_PRESS_STATE) vibrate();
					old_btnStates[iv.getValue()] = btnStates[iv.getValue()];
				}
			}
		}
	}

	public Boolean isHandledStick(){
		boolean hideStick = (mm.getMainHelper().getscrOrientation() == Configuration.ORIENTATION_LANDSCAPE ||
			mm.getMainHelper().getscrOrientation() == Configuration.ORIENTATION_PORTRAIT && Emulator.isPortraitFull())
			&&
			(mm.getPrefsHelper().isHideStick()
				|| mm.getInputHandler().isHideTouchController()
				|| (mm.getPrefsHelper().isTouchLightgun() && !Emulator.isInMenu())
				|| (mm.getPrefsHelper().isTouchGameMouse() && !Emulator.isInMenu())
			)
			&& !ControlCustomizer.isEnabled() ;

		if(mm.getInputHandler().getTouchStick().getMotionPid() != -1) hideStick = false;

		return !hideStick;
	}

	public Boolean isHandledTouchItem(InputValue v){
		boolean handle = false;
		boolean handleStick = isHandledStick();
		int type = v.getType();

		if(type == TouchController.TYPE_ANALOG_RECT ){
			handle = handleStick && mm.getPrefsHelper().getControllerType() != PrefsHelper.PREF_DIGITAL_DPAD;
		}
		else if(type == TouchController.TYPE_STICK_IMG || type == TouchController.TYPE_STICK_RECT) {
			handle = handleStick && mm.getPrefsHelper().getControllerType() == PrefsHelper.PREF_DIGITAL_DPAD;
		}
		else if(type == TouchController.TYPE_BUTTON_IMG || type  == TouchController.TYPE_BUTTON_RECT ){

			if(mm.getMainHelper().getscrOrientation() == Configuration.ORIENTATION_PORTRAIT && !Emulator.isPortraitFull()
				|| ControlCustomizer.isEnabled()) {
				handle = true;
			} else {
				if(mm.getPrefsHelper().isDisabledAllButtonsInFronted() &&
					mm.getInputHandler().getGameController().isEnabled() && !Emulator.isInGame() )
					return false;

				if(mm.getPrefsHelper().isDisabledAllButtonsInGame() &&
					mm.getInputHandler().getGameController().isEnabled() && Emulator.isInGame() )
					return false;

				handle = true;
				int n;

				if (mm.getInputHandler().isHideTouchController() ||
					(mm.getPrefsHelper().isTouchLightgun() && !Emulator.isInMenu()) ||
					(mm.getPrefsHelper().isTouchGameMouse() && !Emulator.isInMenu())) {
					n = 0;
				} else if (Emulator.isSaveorload()) {
					n = 5;
				} else if (!Emulator.isInGame()){
					if(mm.getPrefsHelper().getNumButtons() > 2) n = mm.getPrefsHelper().getNumButtons();
					else if(mm.getPrefsHelper().isHideStick()) n = 0;
					else n = 2;
				} else {
					n = mm.getPrefsHelper().getNumButtons();
					if (n == -1) {
						n = Emulator.getValue(Emulator.NUMBTNS);
						if (n <= 2) n = 2;
						else if (n <= 4) n = 4;
						else n = 6;
					}
					if(Emulator.isInMenu() && n < 2) n = 2;
				}

				// Button visibility filtering based on machine capabilities or user preference
				int b = v.getValue();
				if (b == IController.BTN_D && n < 4) handle=false;
				if (b == IController.BTN_C && n < 3) handle=false;
				if (b == IController.BTN_B && n < 2) handle=false;
				if (b == IController.BTN_A && n < 1) handle=false;

				if (b == IController.BTN_E && n < 5) handle=false;
				if (b == IController.BTN_F && n < 5) handle=false;

				if (b == IController.BTN_G && n < 5 && !mm.getPrefsHelper().isAlwaysGH()) handle=false;
				if (b == IController.BTN_H && n < 5 && !mm.getPrefsHelper().isAlwaysGH()) handle=false;
			}
		}
		return handle;
	}

	protected void fixControllerCoords(ArrayList<InputValue> values) {
		if (values != null) {
			for (int i = 0; i < values.size(); i++) {
				values.get(i).setFixData(dx, dy, ax, ay);
				if (values.get(i).getType() == TYPE_ANALOG_RECT)
					mm.getInputHandler().getTouchStick().setStickArea(values.get(i).getRect());
			}
		}
	}

	public void setFixFactor(int ax, int ay, float dx, float dy) {
		this.ax = ax;
		this.ay = ay;
		this.dx = dx;
		this.dy = dy;
		fixControllerCoords(values);
	}

	protected void setButtonsSizes(ArrayList<InputValue> values) {
		if (mm.getMainHelper().getscrOrientation() == Configuration.ORIENTATION_PORTRAIT && !Emulator.isPortraitFull())
			return;

		int sz = 0;
		switch (mm.getPrefsHelper().getButtonsSize()) {
			case 1: sz = -30; break;
			case 2: sz = -20; break;
			case 3: sz = 0; break;
			case 4: sz = 20; break;
			case 5: sz = 30; break;
		}

		int sz2 = 0;
		switch (mm.getPrefsHelper().getStickSize()) {
			case 1: sz2 = -30; break;
			case 2: sz2 = -20; break;
			case 3: sz2 = 0; break;
			case 4: sz2 = 20; break;
			case 5: sz2 = 30; break;
		}

		if (values == null || (sz == 0 && sz2 == 0)) return;

		for (int j = 0; j < values.size(); j++) {
			InputValue iv = values.get(j);

			if (iv.getType() == TYPE_BUTTON_IMG || iv.getType() == TYPE_BUTTON_RECT) {
				if (iv.getValue() != BTN_EXIT && iv.getValue() != BTN_OPTION && iv.getValue() != BTN_START && iv.getValue() != BTN_COIN)
					iv.setSize(0, 0, sz, sz);
			} else if (iv.getType() == TYPE_STICK_IMG) {
				iv.setSize(0, 0, sz2, sz2);
			} else if (iv.getType() == TYPE_STICK_RECT) {
				switch (iv.getValue()) {
					case 1: iv.setSize(0, 0, 0, 0); break; //upleft
					case 2: iv.setSize(0, 0, sz2, 0); break; //up
					case 3: iv.setSize(sz2, 0, sz2, 0); break; //upright
					case 4: iv.setSize(0, 0, sz2 / 2, sz2); break; //left
					case 5: iv.setSize(sz2 / 2, 0, sz2, sz2); break; //right
					case 6: iv.setSize(0, sz2, 0, sz2); break; //downleft
					case 7: iv.setSize(0, sz2, sz2, sz2); break; //down
					case 8: iv.setSize(sz2, sz2, sz2, sz2); break; //downright
					default: iv.setSize(0, 0, sz2, sz2);
				}
			} else if (iv.getType() == TYPE_ANALOG_RECT) {
				iv.setSize(0, 0, sz2, sz2);
				mm.getInputHandler().getTouchStick().setStickArea(iv.getRect());
			}
		}
	}

	public void readControllerValues(int v) {
		readInputValues(v, values);
		fixControllerCoords(values);
		setButtonsSizes(values);
		if (mm.getInputHandler().getControlCustomizer() != null)
			mm.getInputHandler().getControlCustomizer().readDefinedControlLayout();
	}

	protected void readInputValues(int id, ArrayList<InputValue> values) {
		InputStream is = mm.getResources().openRawResource(id);
		InputStreamReader isr = new InputStreamReader(is);
		BufferedReader br = new BufferedReader(isr);

		InputValue iv = null;
		values.clear();

		try {
			String s = br.readLine();
			while (s != null) {
				int[] data = new int[10];

				// Skip commented layout definitions
				if (s.trim().startsWith("//")) {
					s = br.readLine();
					continue;
				}

				StringTokenizer st = new StringTokenizer(s, ",");
				int j = 0;
				while (st.hasMoreTokens()) {
					String token = st.nextToken();
					int k = token.indexOf("/");
					if (k != -1) {
						token = token.substring(0, k);
					}

					token = token.trim();
					if (token.equals("")) break;

					data[j] = Integer.parseInt(token);
					j++;
					if (k != -1) break;
				}

				if (j != 0) {
					iv = new InputValue(data, mm);
					values.add(iv);
				}
				s = br.readLine();
			}
		} catch (IOException e) {
			Log.e(TAG, "Failed to parse on-screen controller layout file", e);
		}
	}

	protected void vibrate() {
		Vibrator vibrator = (Vibrator) mm.getSystemService(Context.VIBRATOR_SERVICE);
		if (vibrator == null || !vibrator.hasVibrator()) return;

		// Use the modern Haptic API for supported devices, with a safe fallback for older APIs
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
			vibrator.vibrate(VibrationEffect.createPredefined(VibrationEffect.EFFECT_CLICK));
		} else {
			// Legacy fallback
			vibrator.vibrate(20);
		}
	}
}
