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

import android.content.SharedPreferences;
import android.graphics.Color;
import android.util.SparseIntArray;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.content.Context;
import android.hardware.input.InputManager;

import java.util.HashMap;
import android.util.Log;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.R;
import com.seleuco.mame4droid.helpers.DialogHelper;
import com.seleuco.mame4droid.helpers.MainHelper;
import com.seleuco.mame4droid.helpers.PrefsHelper;
import com.seleuco.mame4droid.widgets.WarnWidget;

import org.json.JSONObject;

import java.util.Arrays;

public class GameController implements IController {

	private static final String TAG = "GameController";
	private static final int FIRST_PERSISTENT_ID = 1000;

	// Global profile fallback (when true, all controllers share ID 9999)
	protected static Boolean fakeID = false;

	// Memory state for dynamic assignment and persistence
	protected static HashMap<Integer, String> genericControllers = new HashMap<>();
	protected static HashMap<String, Integer> persistentIDs = new HashMap<>();
	protected static int nextPersistentID = FIRST_PERSISTENT_ID;

	// Standard MAME arcade inputs
	protected static final int[] emulatorInputValues = {
		UP_VALUE, DOWN_VALUE, LEFT_VALUE, RIGHT_VALUE,
		A_VALUE, B_VALUE, C_VALUE, D_VALUE,
		E_VALUE, F_VALUE, G_VALUE, H_VALUE,
		COIN_VALUE, START_VALUE, EXIT_VALUE, OPTION_VALUE
	};

	// Factory default profile. We use Device ID 0 to keep it isolated from user customizations.
	public static int[] defaultKeyMapping = {
		makeKeyCodeWithDeviceID(0,KeyEvent.KEYCODE_DPAD_UP),
		makeKeyCodeWithDeviceID(0,KeyEvent.KEYCODE_DPAD_DOWN),
		makeKeyCodeWithDeviceID(0,KeyEvent.KEYCODE_DPAD_LEFT),
		makeKeyCodeWithDeviceID(0,KeyEvent.KEYCODE_DPAD_RIGHT),
		makeKeyCodeWithDeviceID(0,KeyEvent.KEYCODE_BUTTON_B),
		makeKeyCodeWithDeviceID(0,KeyEvent.KEYCODE_BUTTON_A),
		makeKeyCodeWithDeviceID(0,KeyEvent.KEYCODE_BUTTON_X),
		makeKeyCodeWithDeviceID(0,KeyEvent.KEYCODE_BUTTON_Y),
		makeKeyCodeWithDeviceID(0,KeyEvent.KEYCODE_BUTTON_L1),
		makeKeyCodeWithDeviceID(0,KeyEvent.KEYCODE_BUTTON_R1),
		makeKeyCodeWithDeviceID(0,KeyEvent.KEYCODE_BUTTON_L2),
		makeKeyCodeWithDeviceID(0,KeyEvent.KEYCODE_BUTTON_R2),
		makeKeyCodeWithDeviceID(0,KeyEvent.KEYCODE_BUTTON_THUMBR),
		makeKeyCodeWithDeviceID(0,KeyEvent.KEYCODE_BUTTON_THUMBL),
		makeKeyCodeWithDeviceID(0, KeyEvent.KEYCODE_BACK),
		makeKeyCodeWithDeviceID(0, KeyEvent.KEYCODE_MENU),
		// Empty padding for remaining slots...
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	};

	public static int[] keyMapping = new int[emulatorInputValues.length * 4];

	static protected int MAX_DEVICES = 4;
	static protected int MAX_KEYS = 250;
	protected float MY_PI = 3.14159265f;

	protected int[] oldinput = new int[MAX_DEVICES], newinput = new int[MAX_DEVICES];

	// Maps Android's volatile hardware ID to a player slot (0 to 3)
	public static int[] deviceIDs = new int[MAX_DEVICES];

	static boolean joystickMotion = false;

	protected int[][] deviceMappings = new int[MAX_KEYS][MAX_DEVICES];
	protected static SparseIntArray banDev = new SparseIntArray(50);
	static protected MAME4droid mm = null;


	// =========================================================================
	// LIFECYCLE & INITIALIZATION
	// =========================================================================

	public void setMAME4droid(MAME4droid value) {
		mm = value;
		if(mm==null) return;

		loadPeristentsIDs();
		fakeID = mm.getPrefsHelper().isFakeID();

		InputManager inputManager = (InputManager) mm.getSystemService(Context.INPUT_SERVICE);
		if (inputManager != null) {
			// Attach to MainLooper to ensure UI messages are safely dispatched on the main thread
			inputManager.registerInputDeviceListener(new InputManager.InputDeviceListener() {
				@Override
				public void onInputDeviceAdded(int deviceId) {}

				@Override
				public void onInputDeviceRemoved(int deviceId) {
					banDev.delete(deviceId);
					genericControllers.remove(deviceId);

					for (int i = 0; i < MAX_DEVICES; i++) {
						if (deviceIDs[i] == deviceId) {
							deviceIDs[i] = -1; // Free up the player slot
							joystickMotion = false;

							Emulator.setDigitalData(i, 0);
							Emulator.setAnalogData(Emulator.LEFT_STICK_DATA, i, 0.0f, 0.0f);
							Emulator.setAnalogData(Emulator.RIGHT_STICK_DATA, i, 0.0f, 0.0f);
							Emulator.setAnalogData(Emulator.TRIGGER_DATA, i, 0.0f, 0.0f);

							final int playerNum = i + 1;

							mm.runOnUiThread(new Runnable() {
								@Override
								public void run() {
									String msg = mm.getString(R.string.controller_disconnected, playerNum);
									new WarnWidget.WarnWidgetHelper(mm, msg, 3, Color.YELLOW, true);
									mm.getMainHelper().updateMAME4droid();
								}
							});
							return;
						}
					}
				}
				@Override
				public void onInputDeviceChanged(int deviceId) {}
			}, new android.os.Handler(android.os.Looper.getMainLooper()));
		}
		resetAutodetected();
	}

	public static void resetAutodetected() {
		Arrays.fill(deviceIDs, -1);
		banDev.clear();
		genericControllers.clear();
		joystickMotion = false;
	}

	public boolean isEnabled() {
		int numDevs = 0;
		for (int i = 0; i < MAX_DEVICES; i++) {
			if (deviceIDs[i] != -1) {
				numDevs++;
			}
		}
		return numDevs != 0 || joystickMotion;
	}


	// =========================================================================
	// HARDWARE IDENTITY & PERSISTENCE
	// =========================================================================

	/**
	 * Generates or retrieves a persistent, deterministic Virtual ID for a controller.
	 * It uses the hardware descriptor hash to ensure the ID survives reboots and disconnects.
	 */
	public static int getPersistentDeviceId(InputDevice idev) {
		if (idev == null) return 0;

		// Master Global Profile ID. Prevents collisions with defaults (0) when fakeID is enabled.
		if (fakeID) return 9999;

		try {
			String descriptor = idev.getDescriptor();
			//Log.d(TAG, "Analyzing device: " + idev.getName() + " (Android ID: " + idev.getId() + ")");
			//Log.d(TAG, "Hardware Descriptor: [" + descriptor + "]");

			// Sanity check: ensure descriptor isn't garbage
			if (descriptor != null && descriptor.length() > 4 && !descriptor.equalsIgnoreCase("unknown")) {

				if (persistentIDs.containsKey(descriptor)) {
					int existingId = persistentIDs.get(descriptor);
					//Log.d(TAG, "-> KNOWN CONTROLLER! Returning Persisted ID: " + existingId);
					return existingId;
				} else {
					// First time seeing this gamepad. Assign a new sequential virtual ID.
					int newId = nextPersistentID++;
					Log.d(TAG, "-> NEW CONTROLLER DETECTED! Generating and saving new persisted ID: " + newId);
					persistentIDs.put(descriptor, newId);
					savePersistentsIDs();
					return newId;
				}
			} else {
				Log.d(TAG, "-> WARNING: Trash or empty descriptor. Aborting persistence.");
			}
		} catch (Exception ignored) {}

		// Fallback for badly implemented gamepads without descriptors
		int iControllerNumber = idev.getControllerNumber();
		Log.d(TAG, "-> Using volatile Android fallback. iControllerNumber: " + iControllerNumber);
		if (iControllerNumber > 0) return iControllerNumber;

		return idev.getId();
	}

	/**
	 * Public getter used primarily by the mapping UI (KeySelect) to bind keys to the correct Virtual ID.
	 */
	public static int getControllerId(InputDevice idev) {
		if (idev == null) return 0;
		int value = getPersistentDeviceId(idev);
		Log.d(TAG, "Controller id is " + value);
		return value;
	}

	public static void loadPeristentsIDs() {
		if(mm == null) return;
		try {
			SharedPreferences prefs = mm.getSharedPreferences("mame4droid_prefs", Context.MODE_PRIVATE);
			String jsonStr = prefs.getString("persistents_ids", "{}");
			JSONObject json = new JSONObject(jsonStr);
			persistentIDs.clear();

			Log.d(TAG, "=== LOADING PERSISTED CONTROLLERS ===");

			java.util.Iterator<String> keys = json.keys();
			while (keys.hasNext()) {
				String k = keys.next();
				if (k.equals("nextPersistentID")) {
					nextPersistentID = json.getInt(k);
				} else {
					persistentIDs.put(k, json.getInt(k));
				}
				Log.d(TAG, "Loaded -> Descriptor: [" + k + "] = Virtual ID: " + json.getInt(k));
			}
			Log.d(TAG, "Next available ID will be: " + nextPersistentID);
			Log.d(TAG, "=======================================");
		} catch (Exception ignored) {
			Log.e(TAG, "Error while loading persisted controllers.");
		}
	}

	public static void savePersistentsIDs() {
		if (mm == null) return;
		try {
			JSONObject json = new JSONObject();
			Log.d(TAG, "=== SAVING PERSISTED CONTROLLERS ===");
			for (java.util.Map.Entry<String, Integer> entry : persistentIDs.entrySet()) {
				json.put(entry.getKey(), entry.getValue());
				Log.d(TAG, "Saving -> Descriptor: [" + entry.getKey() + "] = Virtual ID: " + entry.getValue());
			}
			json.put("nextPersistentID", nextPersistentID);

			SharedPreferences prefs = mm.getSharedPreferences("mame4droid_prefs", Context.MODE_PRIVATE);
			prefs.edit().putString("persistents_ids", json.toString()).apply();
			Log.d(TAG, "Save completed in SharedPreferences.");
			Log.d(TAG, "====================================");
		} catch (Exception ignored) {
			Log.e(TAG, "Error while saving persisted controllers.");
		}
	}

	public static void clearPersistentsIDs() {
		if (mm == null) return;
		try {
			Log.d(TAG, "=== CLEARING PERSISTED CONTROLLERS ===");

			// Wipe RAM state
			persistentIDs.clear();
			nextPersistentID = FIRST_PERSISTENT_ID;

			Log.d(TAG, "RAM cleared: persistentIDs empty, nextPersistentID reset to " + FIRST_PERSISTENT_ID + ".");

			// Wipe Storage
			SharedPreferences prefs = mm.getSharedPreferences("mame4droid_prefs", Context.MODE_PRIVATE);
			prefs.edit().remove("persistents_ids").apply();

			Log.d(TAG, "Storage cleared: 'persistents_ids' removed from SharedPreferences.");
			Log.d(TAG, "======================================");

		} catch (Exception ignored) {
			Log.e(TAG, "Error while clearing persisted controllers.");
		}
	}

	// =========================================================================
	// BITWISE PACKING (Virtual ID + KeyCode)
	// =========================================================================

	// Packs the device ID (upper 16 bits) and the physical key code (lower 16 bits) into a single integer.
	public static int makeKeyCodeWithDeviceID(InputDevice id, int iKeyCode) {
		int value = 0;
		try {
			value = getControllerId(id);
		} catch (Exception ignored) {}
		return makeKeyCodeWithDeviceID(value, iKeyCode);
	}

	public static int makeKeyCodeWithDeviceID(int iDeviceId, int iKeyCode) {
		int iRet = iDeviceId;
		iRet = iRet << 16;
		iRet |= iKeyCode;
		return iRet;
	}

	public static void getInfoFromKeyCodeWithDeviceID(int iKeyCode, int[] iArrRet) {
		iArrRet[0] = iKeyCode >> 16;
		iArrRet[1] = iKeyCode & 0xFFFF;
	}

	public static int getDeviceIdFromKeyCodeWithDeviceID(int iKeyCode) {
		return iKeyCode >> 16;
	}

	public static int getKeyCodeFromKeyCodeWithDeviceID(int iKeyCode) {
		return iKeyCode & 0xFFFF;
	}


	// =========================================================================
	// DYNAMIC CONTROLLER REGISTRATION (The "Seating" Logic)
	// =========================================================================

	protected int checkAndRegisterDevice(InputDevice device) {
		// Ignore virtual keyboards and injected software events (null or ID -1)
		// to prevent them from stealing physical hardware slots.
		if (device == null || device.getId() == -1) return -1;

		int currentId = device.getId();

		//  Always check first if the user has defined a custom profile
		int virtualId = getPersistentDeviceId(device);
		boolean hasCustomProfile = false;
		for (int mappedVal : keyMapping) {
			if (mappedVal != -1) {
				int devId = getDeviceIdFromKeyCodeWithDeviceID(mappedVal);
				int kCode = getKeyCodeFromKeyCodeWithDeviceID(mappedVal);
				if (devId == virtualId && kCode != 0xFFFF && kCode != 0) {
					hasCustomProfile = true;
					break;
				}
			}
		}

		// If a custom profile exists, force the dynamic bridge routing
		if (hasCustomProfile) {
			// Reclaim the controller from autodetect if necessary
			if (!genericControllers.containsKey(currentId)) {
				registerGenericController(device, true);
			}
			return -1; // -1 forces execution into the dynamic bridge path
		}

		// Already registered as a pure Dynamic/Generic controller?
		if (genericControllers.containsKey(currentId)) return -1;

		// Already registered via Autodetect?
		for (int i = 0; i < MAX_DEVICES; i++) {
			if (deviceIDs[i] == currentId) return i;
		}

		// --- FIRST TIME SIGNAL FROM THIS DEVICE ---

		// Attempt Autodetect ONLY if globally enabled
		boolean attemptLegacy = mm.getPrefsHelper().isContollerAutodetect();
		int dev = -1;

		if (attemptLegacy) {
			dev = detectDevice(device);
		}

		// If not, register dynamically (Also filters out raw volume button presses)
		if (dev == -1) {
			int sources = device.getSources();
			boolean isGamepad = (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD;
			boolean isJoystick = (sources & InputDevice.SOURCE_CLASS_JOYSTICK) == InputDevice.SOURCE_CLASS_JOYSTICK;

			// Only claim a player slot if it's officially categorized as gaming hardware
			if (isGamepad || isJoystick) {
				registerGenericController(device, false);
			}
		}

		return dev;
	}

	/**
	 * Assigns a generic controller to the first available player slot (0 to 3)
	 * and checks if the user has created a custom mapping profile for it.
	 */
	protected void registerGenericController(InputDevice device, boolean hasCustomProfile) {
		if (device == null) return;
		int currentId = device.getId();

		if (!genericControllers.containsKey(currentId)) {
			int activeSlot = -1;
			for (int i = 0; i < MAX_DEVICES; i++) {
				if (deviceIDs[i] == currentId) { activeSlot = i; break; }
			}
			if (activeSlot == -1) {
				for (int i = 0; i < MAX_DEVICES; i++) {
					if (deviceIDs[i] == -1) { deviceIDs[i] = currentId; activeSlot = i; break; }
				}
			}

			String slotName = (activeSlot != -1) ? "P" + (activeSlot + 1) : mm.getString(R.string.controller_unassigned);
			String text;

			if (hasCustomProfile) {
				text = mm.getString(R.string.controller_detected_custom, slotName);
				new WarnWidget.WarnWidgetHelper(mm, text, 3, Color.GREEN, true);
			} else {
				text = mm.getString(R.string.controller_detected_defaults, slotName);
				new WarnWidget.WarnWidgetHelper(mm, text, 3, Color.YELLOW, true);
			}

			genericControllers.put(currentId, slotName);
			mm.getMainHelper().updateMAME4droid();
		}
	}

	protected int getDevice(InputDevice device, boolean detect) {
		if (!mm.getPrefsHelper().isContollerAutodetect()) return -1;
		if (device == null || device.getId() == -1) return -1;

		for (int i = 0; i < MAX_DEVICES; i++) {
			if (deviceIDs[i] == device.getId())
				return i;
		}

		return detect ? detectDevice(device) : -1;
	}


	// =========================================================================
	// CORE INPUT ROUTING & BRIDGING
	// =========================================================================

	protected void setContollerData(int i, KeyEvent event, int data, int[]digital_data) {
		int action = event.getAction();
		if (action == KeyEvent.ACTION_DOWN)
			digital_data[i] |= data;
		else if (action == KeyEvent.ACTION_UP)
			digital_data[i] &= ~data;
	}

	protected boolean handleControllerKey(int value, KeyEvent event, int []digital_data) {
		int v = emulatorInputValues[value % emulatorInputValues.length];

		if (v == EXIT_VALUE) {
			if (event.getAction() == KeyEvent.ACTION_UP) {
				Emulator.setValue(Emulator.EXIT_GAME, 1);
				try { Thread.sleep(InputHandler.PRESS_WAIT); } catch (InterruptedException ignored) {}
				Emulator.setValue(Emulator.EXIT_GAME, 0);
			}
		} else if (v == OPTION_VALUE ) {
			if (event.getAction() == KeyEvent.ACTION_UP && !Emulator.isInOptions()) {
				Emulator.setInOptions(true);
				mm.showDialog(DialogHelper.DIALOG_OPTIONS);
			}
		} else {
			int i = value / emulatorInputValues.length;
			setContollerData(i, event, v, digital_data);
			mm.getInputHandler().fixTiltCoin();
			Emulator.setDigitalData(i, digital_data[i]);
		}
		return true;
	}

	public boolean handleGameController(int keyCode, KeyEvent event, int[] digital_data) {
		InputDevice device = event.getDevice();

		// Maintain visual joystick logic (Protected against null devices)
		int sources = (device != null) ? device.getSources() : 0;
		boolean isGamepad = (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD;
		boolean isJoystick = (sources & InputDevice.SOURCE_CLASS_JOYSTICK) == InputDevice.SOURCE_CLASS_JOYSTICK;

		if (isGamepad || isJoystick) {
			if (!joystickMotion) {
				joystickMotion = true;
				mm.getMainHelper().updateMAME4droid();
			}
		}

		// Input Gatekeeper: Evaluates hardware and routes to Autodetect or Dynamic Bridge
		int dev = checkAndRegisterDevice(device);
		boolean manageDevice = (dev != -1);

		if (!manageDevice) {
			// --- THE DYNAMIC BRIDGE ---
			int virtualId = getPersistentDeviceId(device);
			int actionIndex = -1;
			boolean hasCustomProfile = false;

			// Query Custom Profile
			for (int i = 0; i < keyMapping.length; i++) {
				int mappedVal = keyMapping[i];
				if (mappedVal != -1) {
					int devId = getDeviceIdFromKeyCodeWithDeviceID(mappedVal);
					int kCode = getKeyCodeFromKeyCodeWithDeviceID(mappedVal);

					if (devId == virtualId && kCode != 0xFFFF && kCode != 0) {
						hasCustomProfile = true;
						if (kCode == keyCode) {
							actionIndex = i % emulatorInputValues.length;
							break;
						}
					}
				}
			}

			// Query Factory Defaults (Only fallback if no custom profile exists)
			if (actionIndex == -1 && !hasCustomProfile) {
				for (int i = 0; i < defaultKeyMapping.length; i++) {
					if (defaultKeyMapping[i] == makeKeyCodeWithDeviceID(0, keyCode)) {
						actionIndex = i % emulatorInputValues.length;
						break;
					}
				}
			}

			// Route the resolved action
			if (actionIndex != -1) {
				int activeSlot = -1;

				if (device == null || device.getId() == -1) {
					// Virtual keyboard or on-screen touch controls.
					// Always route these to Player 1 to ensure playability.
					activeSlot = 0;
				} else {
					// Look up the physical slot assigned to this device
					for (int j = 0; j < MAX_DEVICES; j++) {
						if (deviceIDs[j] == device.getId()) {
							activeSlot = j;
							break;
						}
					}

					// Fallback for Bluetooth PC keyboards (not flagged as gamepads and lacking a slot).
					// Also prevents a 5th plugged-in controller from overflowing the player array.
					if (activeSlot == -1 && !isGamepad && !isJoystick) {
						activeSlot = 0;
					}
				}

				if (activeSlot != -1) {
					int mappedValue = (activeSlot * emulatorInputValues.length) + actionIndex;
					if (handleControllerKey(mappedValue, event, digital_data)) return true;
				}
			}

			// --- HARDCODED ANDROID FALLBACKS ---
			if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
				handleControllerKey(14, event, digital_data);
				return true;
			}
			if (event.getKeyCode() == KeyEvent.KEYCODE_MENU) {
				handleControllerKey(15, event, digital_data);
				return true;
			}
			if ((event.getKeyCode() == KeyEvent.KEYCODE_BUTTON_START || event.getKeyCode() == KeyEvent.KEYCODE_DPAD_CENTER
				|| event.getKeyCode() == KeyEvent.KEYCODE_BUTTON_SELECT) && !Emulator.isInGame() && mm.getMainHelper().isAndroidTV()) {
				handleControllerKey(15, event, digital_data);
				return true;
			}

			if (hasCustomProfile) {
				return true;
			}

			return false;

		} else {
			// --- AUTODETECT PROCESSING ---
			int v = deviceMappings[event.getKeyCode()][dev];

			if (v != -1) {
				if (v == EXIT_VALUE) {
					if (event.getAction() == KeyEvent.ACTION_UP) {
						Emulator.setValue(Emulator.EXIT_GAME, 1);
						try { Thread.sleep(InputHandler.PRESS_WAIT); } catch (InterruptedException ignored) {}
						Emulator.setValue(Emulator.EXIT_GAME, 0);
					}
				} else if (v == OPTION_VALUE) {
					if (event.getAction() == KeyEvent.ACTION_UP  && !Emulator.isInOptions()) {
						Emulator.setInOptions(true);
						mm.showDialog(DialogHelper.DIALOG_OPTIONS);
					}
				} else {
					int action = event.getAction();
					if (action == KeyEvent.ACTION_DOWN) {
						digital_data[dev] |= v;
					} else if (action == KeyEvent.ACTION_UP) {
						digital_data[dev] &= ~v;
					}

					mm.getInputHandler().fixTiltCoin();
					Emulator.setDigitalData(dev, digital_data[dev]);
				}
				return true;
			}
			return false;
		}
	}


	// =========================================================================
	// ANALOG & STICK PROCESSING
	// =========================================================================

	final public float rad2degree(float r) {
		return ((r * 180.0f) / MY_PI);
	}

	final public float getAngle(float x, float y) {
		float ang = rad2degree((float) Math.atan2(x, y));
		if (ang < 0.0f) ang += 360.0f;
		return ang;
	}

	final public float getMagnitude(float x, float y) {
		return (float) Math.sqrt((x * x) + (y * y));
	}

	protected float processAxis(InputDevice.MotionRange range, float axisvalue) {
		float absaxisvalue = Math.abs(axisvalue);
		float deadzone = range.getFlat();

		if (absaxisvalue <= deadzone) return 0.0f;

		float normalizedvalue;
		if (axisvalue < 0.0f) {
			normalizedvalue = absaxisvalue / range.getMin();
		} else {
			normalizedvalue = absaxisvalue / range.getMax();
		}
		return normalizedvalue;
	}

	final public float getAxisValue(int axis, MotionEvent event, int historyPos) {
		float value = 0.0f;
		InputDevice device = event.getDevice();
		if (device != null) {
			InputDevice.MotionRange range = device.getMotionRange(axis, event.getSource());
			if (range != null) {
				float axisValue;
				if (historyPos >= 0) {
					axisValue = event.getHistoricalAxisValue(axis, historyPos);
				} else {
					axisValue = event.getAxisValue(axis);
				}
				value = this.processAxis(range, axisValue);
			}
		}
		return value;
	}

	protected boolean hasSignificantMovement(MotionEvent event, float threshold) {
		int[] axes = {
			MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ,
			MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y, MotionEvent.AXIS_GAS, MotionEvent.AXIS_BRAKE
		};

		for (int axis : axes) {
			if (Math.abs(getAxisValue(axis, event, -1)) > threshold) {
				return true;
			}
		}
		return false;
	}

	public boolean genericMotion(MotionEvent event, int[] digital_data) {
		if (((event.getSource() & (InputDevice.SOURCE_CLASS_JOYSTICK | InputDevice.SOURCE_GAMEPAD)) == 0)
			|| (event.getAction() != MotionEvent.ACTION_MOVE)) {
			return false;
		}

		InputDevice device = event.getDevice();
		if (device == null) return false; // Anti-crash protection

		if (hasSignificantMovement(event, 0.20f)) {
			if (!joystickMotion) {
				joystickMotion = true;
				mm.getMainHelper().updateMAME4droid();
			}
			// Centralize device registration upon significant analog stick movement
			checkAndRegisterDevice(event.getDevice());
		}

		int historySize = event.getHistorySize();
		for (int i = 0; i < historySize; i++) {
			processStickInput(event, i, digital_data);
		}

		return processStickInput(event, -1, digital_data);
	}

	protected boolean processStickInput(MotionEvent event, int historyPos, int[] digital_data) {
		int ways = mm.getPrefsHelper().getStickWays();
		if (ways == -1) ways = Emulator.getValue(Emulator.NUMWAYS);
		boolean b = Emulator.isInGameButNotInMenu();

		int dev = getDevice(event.getDevice(), false);

		if (dev == -1) { // It's a generic controller, find its dynamic slot
			for (int i = 0; i < MAX_DEVICES; i++) {
				if (deviceIDs[i] == event.getDevice().getId()) {
					dev = i;
					break;
				}
			}
		}

		// Prevent out-of-bounds overflow for extra unseated controllers (e.g. 5th controller)
		if (dev == -1) {
			return false;
		}

		int joy = dev;
		newinput[joy] = 0;

		float deadZone = 0.2f;
		switch (mm.getPrefsHelper().getGamepadDZ()) {
			case 1: deadZone = 0.01f; break;
			case 2: deadZone = 0.15f; break;
			case 3: deadZone = 0.2f; break;
			case 4: deadZone = 0.3f; break;
			case 5: deadZone = 0.5f; break;
		}

		float x = 0.0f, y = 0.0f, mag = 0.0f;

		for (int i = 0; i < 2; i++) {
			if (i == 0 && mm.getInputHandler().getTiltSensor().isEnabled() && Emulator.isInGameButNotInMenu())
				continue;

			if (i == 0) {
				x = getAxisValue(MotionEvent.AXIS_X, event, historyPos);
				y = getAxisValue(MotionEvent.AXIS_Y, event, historyPos);
			} else {
				x = getAxisValue(MotionEvent.AXIS_HAT_X, event, historyPos);
				y = getAxisValue(MotionEvent.AXIS_HAT_Y, event, historyPos);
			}

			mag = getMagnitude(x, y);

			if (mag >= deadZone) {
				if (i == 0) {
					if (mm.getPrefsHelper().getControllerType() != PrefsHelper.PREF_DIGITAL_STICK) {
						Emulator.setAnalogData(Emulator.LEFT_STICK_DATA, joy, x, y * -1.0f);
						continue;
					}
				}

				float v = getAngle(x, y);

				// Arcade stick way-restrictions
				if (ways == 2 && b) {
					if (v < 180) newinput[joy] |= RIGHT_VALUE;
					else if (v >= 180) newinput[joy] |= LEFT_VALUE;
				} else if (ways == 4 || !b) {
					if (v >= 315 || v < 45) newinput[joy] |= DOWN_VALUE;
					else if (v >= 45 && v < 135) newinput[joy] |= RIGHT_VALUE;
					else if (v >= 135 && v < 225) newinput[joy] |= UP_VALUE;
					else if (v >= 225 && v < 315) newinput[joy] |= LEFT_VALUE;
				} else { // 8-way
					if (v >= 330 || v < 30) {
						newinput[joy] |= DOWN_VALUE;
					} else if (v >= 30 && v < 60) {
						newinput[joy] |= DOWN_VALUE;
						newinput[joy] |= RIGHT_VALUE;
					} else if (v >= 60 && v < 120) {
						newinput[joy] |= RIGHT_VALUE;
					} else if (v >= 120 && v < 150) {
						newinput[joy] |= RIGHT_VALUE;
						newinput[joy] |= UP_VALUE;
					} else if (v >= 150 && v < 210) {
						newinput[joy] |= UP_VALUE;
					} else if (v >= 210 && v < 240) {
						newinput[joy] |= UP_VALUE;
						newinput[joy] |= LEFT_VALUE;
					} else if (v >= 240 && v < 300) {
						newinput[joy] |= LEFT_VALUE;
					} else if (v >= 300 && v < 330) {
						newinput[joy] |= LEFT_VALUE;
						newinput[joy] |= DOWN_VALUE;
					}
				}
			} else {
				if (i == 0) {
					Emulator.setAnalogData(Emulator.LEFT_STICK_DATA, joy, 0, 0);
				}
			}
		}

		if (!mm.getPrefsHelper().isDisabledRightStick() && Emulator.isInGame()) {
			x = getAxisValue(MotionEvent.AXIS_Z, event, historyPos);
			y = getAxisValue(MotionEvent.AXIS_RZ, event, historyPos) * -1;
			mag = getMagnitude(x, y);

			if (mag >= deadZone) {
				Emulator.setAnalogData(Emulator.RIGHT_STICK_DATA, joy, x, y);
			} else {
				Emulator.setAnalogData(Emulator.RIGHT_STICK_DATA, joy, 0.0f, 0.0f);
			}
		}

		x = getAxisValue(MotionEvent.AXIS_GAS, event, historyPos);
		y = getAxisValue(MotionEvent.AXIS_BRAKE, event, historyPos);
		Emulator.setAnalogData(Emulator.TRIGGER_DATA, joy, (x * 2.0f) - 1.0f, (y * 2.0f) - 1.0f);

		digital_data[joy] &= ~(oldinput[joy] & ~newinput[joy]);
		digital_data[joy] |= newinput[joy];

		mm.getInputHandler().fixTiltCoin();
		Emulator.setDigitalData(joy, digital_data[joy]);

		oldinput[joy] = newinput[joy];

		return true;
	}


	// =========================================================================
	// AUTODETECT SYSTEM (Hardcoded profiles)
	// =========================================================================

	protected void mapDPAD(int id) {
		deviceMappings[KeyEvent.KEYCODE_DPAD_UP][id] = UP_VALUE;
		deviceMappings[KeyEvent.KEYCODE_DPAD_DOWN][id] = DOWN_VALUE;
		deviceMappings[KeyEvent.KEYCODE_DPAD_LEFT][id] = LEFT_VALUE;
		deviceMappings[KeyEvent.KEYCODE_DPAD_RIGHT][id] = RIGHT_VALUE;
	}

	protected void mapL1R1(int id) {
		deviceMappings[KeyEvent.KEYCODE_BUTTON_L1][id] = E_VALUE;
		deviceMappings[KeyEvent.KEYCODE_BUTTON_R1][id] = F_VALUE;
	}

	protected void mapTHUMBS(int id) {
		deviceMappings[KeyEvent.KEYCODE_BUTTON_THUMBL][id] = START_VALUE;
		deviceMappings[KeyEvent.KEYCODE_BUTTON_THUMBR][id] = COIN_VALUE;
	}

	protected void mapSelectStart(int id) {
		deviceMappings[KeyEvent.KEYCODE_BUTTON_SELECT][id] = EXIT_VALUE;
		deviceMappings[KeyEvent.KEYCODE_BUTTON_START][id] = OPTION_VALUE;
	}

	protected int detectDevice(InputDevice device) {
		boolean detected = false;
		int id = -1;

		for (int i = 0; i < MAX_DEVICES && id == -1; i++) {
			if (deviceIDs[i] == -1) id = i;
		}

		if (id == -1 || device == null || banDev == null) return -1;
		if (banDev.get(device.getId()) == 1) return -1;

		final String name = device.getName();

		if (Emulator.isDebug()) {
			String msg = mm.getString(R.string.input_device_detected, name);
			new WarnWidget.WarnWidgetHelper(mm, msg, 3, Color.GREEN, true);
		}

		CharSequence desc = "";

		if (name.contains("PLAYSTATION(R)3") || name.indexOf("Dualshock3") != -1
			|| name.contains("Sixaxis") || name.contains("Gasia,Co")) {

			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = D_VALUE;

			mapDPAD(id);
			mapL1R1(id);
			mapTHUMBS(id);
			mapSelectStart(id);

			deviceMappings[KeyEvent.KEYCODE_BACK][id] = EXIT_VALUE;
			desc = "Sixaxis";
			detected = true;

		} else if (name.contains("Gamepad 0") || name.contains("Gamepad 1") || name.contains("Gamepad 2")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = D_VALUE;

			mapDPAD(id);
			mapL1R1(id);
			mapTHUMBS(id);
			mapSelectStart(id);

			desc = "Gamepad";
			detected = true;

		} else if (name.contains("nvidia_joypad") || name.contains("NVIDIA Controller")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = D_VALUE;

			mapL1R1(id);
			mapTHUMBS(id);

			deviceMappings[KeyEvent.KEYCODE_BUTTON_START][id] = OPTION_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BACK][id] = EXIT_VALUE;

			desc = "NVIDIA Shield";
			detected = true;

		} else if (name.contains("ipega Extending")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = D_VALUE;

			mapL1R1(id);
			mapTHUMBS(id);

			deviceMappings[KeyEvent.KEYCODE_BUTTON_START][id] = OPTION_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_SELECT][id] = EXIT_VALUE;

			desc = "Ipega Extending Game";
			detected = true;

		} else if (name.contains("X-Box") || name.contains("Xbox")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = D_VALUE;

			mapDPAD(id);
			mapL1R1(id);
			mapTHUMBS(id);
			mapSelectStart(id);

			deviceMappings[KeyEvent.KEYCODE_BACK][id] = EXIT_VALUE;
			desc = "XBox";
			detected = true;

		} else if (name.contains("Logitech") && name.contains("Dual Action")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = D_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = A_VALUE;

			mapL1R1(id);
			mapTHUMBS(id);
			mapSelectStart(id);

			desc = "Dual Action";
			detected = true;

		} else if (name.contains("Logitech") && name.contains("RumblePad 2")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_2][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_1][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_3][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_4][id] = D_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_9][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_10][id] = START_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_5][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_6][id] = F_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_11][id] = OPTION_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_12][id] = EXIT_VALUE;

			desc = "Rumblepad 2";
			detected = true;

		} else if (name.contains("Logitech") && name.contains("Precision")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_2][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_1][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_3][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_4][id] = D_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_5][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_6][id] = F_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_7][id] = OPTION_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_8][id] = EXIT_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_9][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_10][id] = START_VALUE;

			desc = "Logitech Precision";
			detected = true;

		} else if (name.contains("TTT THT Arcade console 2P USB Play")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_1][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_2][id] = D_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_5][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_6][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_3][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_7][id] = F_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_4][id] = OPTION_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_8][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_9][id] = START_VALUE;

			desc = "TTT THT Arcade";
			detected = true;

		} else if (name.contains("TOMMO NEOGEOX Arcade Stick")) {
			mapDPAD(id);

			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_C][id] = D_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_R2][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_L2][id] = START_VALUE;

			desc = "TOMMO Neogeo X Arcade";
			detected = true;

		} else if (name.contains("Onlive Wireless Controller")) {
			mapDPAD(id);

			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = D_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_L1][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_R1][id] = F_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_L1][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BACK][id] = START_VALUE;

			desc = "Onlive Wireless";
			detected = true;

		} else if (name.contains("MadCatz") && name.contains("PC USB Wired Stick")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_C][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = D_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Z][id] = E_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_L1][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_R1][id] = F_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_L2][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_R2][id] = START_VALUE;

			desc = "Madcatz PC USB Stick";
			detected = true;

		} else if (name.contains("Logicool") && name.contains("RumblePad 2")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_C][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = D_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = C_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Z][id] = F_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_L1][id] = OPTION_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_R1][id] = EXIT_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_L2][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_R2][id] = START_VALUE;

			desc = "Logicool Rumblepad 2";
			detected = true;

		} else if (name.contains("Zeemote") && name.contains("Steelseries free")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = D_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_MODE][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_START][id] = START_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_L1][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_R1][id] = F_VALUE;

			desc = "Zeemote Steelseries";
			detected = true;

		} else if (name.contains("HuiJia  USB GamePad")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_3][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_4][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_2][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_1][id] = D_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_7][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_8][id] = F_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_9][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_10][id] = START_VALUE;

			desc = "Huijia USB SNES";
			detected = true;

		} else if (name.contains("Smartjoy Family Super Smartjoy 2")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_3][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_4][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_2][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_1][id] = D_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_7][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_8][id] = F_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_5][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_6][id] = START_VALUE;

			desc = "Super Smartjoy";
			detected = true;

		} else if (name.contains("Jess Tech Dual Analog Rumble Pad")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_3][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_1][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_4][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_2][id] = D_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_5][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_7][id] = F_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_6][id] = OPTION_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_8][id] = EXIT_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_11][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_12][id] = START_VALUE;

			detected = true;

		} else if (name.contains("Microsoft") && name.contains("Dual Strike")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_4][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_2][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_3][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_1][id] = D_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_7][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_8][id] = F_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_9][id] = OPTION_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_6][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_5][id] = START_VALUE;

			desc = "MS Dual Strike";
			detected = true;

		} else if (name.contains("Microsoft") && name.contains("SideWinder")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = D_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_L1][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_R1][id] = F_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_Z][id] = OPTION_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_C][id] = EXIT_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_11][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_12][id] = START_VALUE;

			desc = "MS Sidewinder";
			detected = true;

		} else if (name.contains("WiseGroup") &&
			(name.contains("JC-PS102U") || name.contains("TigerGame")) ||
			name.contains("Game Controller Adapter") || name.contains("Dual USB Joypad") ||
			name.contains("Twin USB Joystick")) {

			deviceMappings[KeyEvent.KEYCODE_BUTTON_13][id] = UP_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_15][id] = DOWN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_16][id] = LEFT_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_14][id] = RIGHT_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_4][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_1][id] = D_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_3][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_2][id] = A_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_7][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_8][id] = F_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_5][id] = OPTION_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_6][id] = EXIT_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_10][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_9][id] = START_VALUE;

			desc = "PlayStation2";
			detected = true;

		} else if (name.contains("MOGA") || name.contains("Moga")) {
			mapDPAD(id);

			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = D_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_L1][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_R1][id] = F_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_SELECT][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_START][id] = START_VALUE;

			desc = "MOGA";
			detected = true;

		} else if (name.contains("OUYA Game Controller")) {
			mapDPAD(id);

			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = D_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;

			deviceMappings[KeyEvent.KEYCODE_MENU][id] = OPTION_VALUE;

			mapL1R1(id);
			mapTHUMBS(id);

			desc = "OUYA";
			detected = true;

		} else if (name.contains("DragonRise")) {
			mapDPAD(id);

			deviceMappings[KeyEvent.KEYCODE_BUTTON_2][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_3][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_4][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_1][id] = D_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_5][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_6][id] = F_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_7][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_8][id] = START_VALUE;

			desc = "DragonRise";
			detected = true;

		} else if (name.contains("Thrustmaster T Mini")) {
			mapDPAD(id);

			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = D_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_C][id] = A_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Z][id] = F_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_L2][id] = OPTION_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_R1][id] = EXIT_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_R2][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_L2][id] = START_VALUE;

			desc = "Thrustmaster T Mini";
			detected = true;

		} else if (name.contains("ADC joystick")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = D_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_L2][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_R2][id] = F_VALUE;

			mapDPAD(id);
			mapL1R1(id);

			deviceMappings[KeyEvent.KEYCODE_BUTTON_SELECT][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_START][id] = START_VALUE;

			desc = "JXD S7800";
			detected = true;

		} else if (name.contains("Green Throttle Atlas")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = D_VALUE;

			mapDPAD(id);
			mapL1R1(id);
			mapTHUMBS(id);
			mapSelectStart(id);

			deviceMappings[KeyEvent.KEYCODE_BACK][id] = EXIT_VALUE;

			desc = "Green Throttle";
			detected = true;

		} else if (name.contains("joy_key") && mm.getMainHelper().getDeviceDetected() == MainHelper.DEVICE_AGAMEPAD2) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = D_VALUE;

			deviceMappings[KeyEvent.KEYCODE_BUTTON_L2][id] = E_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_R2][id] = F_VALUE;

			mapDPAD(id);
			mapL1R1(id);

			deviceMappings[KeyEvent.KEYCODE_BUTTON_SELECT][id] = COIN_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_START][id] = START_VALUE;

			desc = "Archos Gamepad 2";
			detected = true;

		} else if (name.contains("NYKO PLAYPAD") ||
			(name.contains("Broadcom Bluetooth HID") && mm.getMainHelper().getDeviceDetected() == MainHelper.DEVICE_SHIELD)) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = D_VALUE;

			mapL1R1(id);
			mapTHUMBS(id);

			deviceMappings[KeyEvent.KEYCODE_BUTTON_START][id] = OPTION_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BACK][id] = EXIT_VALUE;

			desc = "NYKO PLAYPAD";
			detected = true;

		} else if (name.contains("BSP-D8")) {
			deviceMappings[KeyEvent.KEYCODE_BUTTON_A][id] = B_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_B][id] = A_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_X][id] = C_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_Y][id] = D_VALUE;

			mapDPAD(id);
			mapL1R1(id);
			mapTHUMBS(id);

			deviceMappings[KeyEvent.KEYCODE_BUTTON_SELECT][id] = OPTION_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BUTTON_START][id] = EXIT_VALUE;
			deviceMappings[KeyEvent.KEYCODE_BACK][id] = EXIT_VALUE;

			desc = "BSP-D8";
			detected = true;
		}

		if (detected) {
			Log.d(TAG,"Controller detected: " + device.getName());
			deviceIDs[id] = device.getId();
			id++;

			if (id == 1) mm.getMainHelper().updateMAME4droid();

			CharSequence text = mm.getString(R.string.controller_detected_as, desc, id);
			new WarnWidget.WarnWidgetHelper(mm, text.toString(), 3, Color.GREEN, true);

			return id - 1;
		} else {
			banDev.append(device.getId(), 1);
		}

		return -1;
	}
}
