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

//http://code.google.com/p/andengine/source/diff?spec=svn029966d918208057cef0cffcb84d0e32c3beb646&r=029966d918208057cef0cffcb84d0e32c3beb646&format=side&path=/src/org/anddev/andengine/sensor/orientation/OrientationData.java

//NOTAS: usar acelerometro es suficiente,

package com.seleuco.mame4droid.input;

import java.text.DecimalFormat;

import android.annotation.SuppressLint;
import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.view.Surface;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.helpers.PrefsHelper;

public class TiltSensor {

	DecimalFormat df = new DecimalFormat("000.00");

	protected MAME4droid mm = null;

	public void setMAME4droid(MAME4droid value) {
		mm = value;
	}

	public static String str;

	public static float rx = 0;
	public static float ry = 0;

	private float tilt_x;
	private float tilt_z;
	private float ang_x;
	private float ang_z;

	private boolean fallback = false;
	private boolean init = false;
	private boolean enabled = false;

	public boolean isEnabled() {
		return enabled;
	}

	// Polling rate optimized for gaming (approx 50Hz / 20ms)
	private static final int delay = SensorManager.SENSOR_DELAY_GAME;

	public TiltSensor() {}

	@SuppressLint("InlinedApi")
	public void enable() {
		if (!enabled && mm != null && mm.getPrefsHelper().isTiltSensorEnabled()) {
			SensorManager man = (SensorManager) mm.getApplicationContext().getSystemService(Context.SENSOR_SERVICE);
			Sensor acc_sensor = null;

			// Prefer the hardware Gravity sensor as it excludes linear acceleration (user shaking the device)
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.GINGERBREAD) {
				acc_sensor = man.getDefaultSensor(Sensor.TYPE_GRAVITY);
			}

			// Fallback to raw Accelerometer for very old or budget devices
			if (acc_sensor == null) {
				acc_sensor = man.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
				fallback = true;
			}
			enabled = man.registerListener(listen, acc_sensor, delay);
		}
	}

	public void disable() {
		if (enabled && mm != null) {
			SensorManager man = (SensorManager) mm.getApplicationContext().getSystemService(Context.SENSOR_SERVICE);
			man.unregisterListener(listen);
			enabled = false;
		}
	}

	int old_x = 0;
	int old_y = 0;

	protected void reset() {
		old_x = 0;
		old_y = 0;
		tilt_x = 0;
		tilt_z = 0;
		mm.getInputHandler().digital_data[0] = 0;
		Emulator.setDigitalData(0, 0);
		mm.getInputHandler().getTouchController().handleImageStates(true, mm.getInputHandler().digital_data);
		rx = 0;
		ry = 0;
		Emulator.setAnalogData(Emulator.LEFT_STICK_DATA, 0, 0, 0);
	}

	// Primary sensor callback.
	private final SensorEventListener listen = new SensorEventListener() {
		public void onSensorChanged(SensorEvent e) {
			if (mm == null) return;

			PrefsHelper pH = mm.getPrefsHelper();

			// Low-Pass Filter (LPF) constant for the raw accelerometer fallback
			final float alpha = 0.1f;

			float value_x = -e.values[0];
			float value_z = pH.isTiltSwappedYZ() ? e.values[1] : e.values[2];

			// Compensate for the physical orientation of the display
			try {
				int rotation = mm.getWindowManager().getDefaultDisplay().getRotation();
				switch (rotation) {
					case Surface.ROTATION_0:   value_x = -e.values[0]; break;
					case Surface.ROTATION_90:  value_x =  e.values[1]; break;
					case Surface.ROTATION_180: value_x =  e.values[0]; break;
					case Surface.ROTATION_270: value_x = -e.values[1]; break;
				}
			} catch (Throwable ignored) {}

			// Apply Low-Pass Filter ONLY if using the raw accelerometer.
			// This mathematically strips out sudden shaking, isolating gravity.
			if (fallback) {
				tilt_x = alpha * tilt_x + (1 - alpha) * value_x;
				tilt_z = alpha * tilt_z + (1 - alpha) * value_z;
			} else {
				tilt_x = value_x;
				tilt_z = value_z;
			}

			if (pH.isTiltInvertedX()) {
				tilt_x *= -1;
			}

			float deadZone = getDZ();
			boolean run = Emulator.isInGameButNotInMenu() && !Emulator.isPaused();

			if (run) {
				if (!init) {
					reset();
					init = true;
				}

				// Prevent accidental tilt inputs while the user is inserting coins or starting a game
				boolean skip = (mm.getInputHandler().digital_data[0] & IController.COIN_VALUE) != 0 ||
					(mm.getInputHandler().digital_data[0] & IController.START_VALUE) != 0;

				// --- X AXIS (Horizontal) ---
				if (Math.abs(tilt_x) < deadZone || skip) {
					mm.getInputHandler().digital_data[0] &= ~IController.LEFT_VALUE;
					mm.getInputHandler().digital_data[0] &= ~IController.RIGHT_VALUE;
					old_x = 0;
					rx = 0;
				} else if (tilt_x < 0) {
					mm.getInputHandler().digital_data[0] |= IController.LEFT_VALUE;
					mm.getInputHandler().digital_data[0] &= ~IController.RIGHT_VALUE;
					old_x = 1;
				} else {
					mm.getInputHandler().digital_data[0] &= ~IController.LEFT_VALUE;
					mm.getInputHandler().digital_data[0] |= IController.RIGHT_VALUE;
					old_x = 2;
				}

				// --- Z AXIS (Vertical / Pitch) ---
				float neutralZ = getNeutralPos();

				if (Math.abs(tilt_z - neutralZ) < deadZone || skip) {
					mm.getInputHandler().digital_data[0] &= ~IController.UP_VALUE;
					mm.getInputHandler().digital_data[0] &= ~IController.DOWN_VALUE;
					old_y = 0;
					ry = 0;
				} else if ((tilt_z - neutralZ) > 0) {
					mm.getInputHandler().digital_data[0] |= IController.UP_VALUE;
					mm.getInputHandler().digital_data[0] &= ~IController.DOWN_VALUE;
					old_y = 1;
				} else {
					mm.getInputHandler().digital_data[0] &= ~IController.UP_VALUE;
					mm.getInputHandler().digital_data[0] |= IController.DOWN_VALUE;
					old_y = 2;
				}

				Emulator.setDigitalData(0, mm.getInputHandler().digital_data[0]);
				mm.getInputHandler().getTouchController().handleImageStates(true, mm.getInputHandler().digital_data);

				// --- ANALOG MAPPING ---
				if (pH.isTiltAnalog() && !skip) {
					if (Math.abs(tilt_x) >= deadZone) {
						rx = tilt_x / getSensitivity();
						rx = Math.max(-1.0f, Math.min(1.0f, rx)); // Clamp strictly to [-1.0, 1.0]
					}

					if (Math.abs(tilt_z - neutralZ) >= deadZone) {
						ry = (tilt_z - neutralZ) / getSensitivity();
						ry = Math.max(-1.0f, Math.min(1.0f, ry)); // Clamp strictly to [-1.0, 1.0]
					}
				}

				Emulator.setAnalogData(Emulator.LEFT_STICK_DATA, 0, rx, ry);
			}

			if (!run) {
				if (old_x != 0 || old_y != 0) reset();
				init = false;
			}

			// OSD Debug telemetry
			if (Emulator.isDebug()) {
				ang_x = (float) Math.toDegrees(Math.atan(9.81f / tilt_x) * 2);
				ang_x = ang_x < 0 ? -(ang_x + 180) : 180 - ang_x;
				ang_z = (float) Math.toDegrees(Math.atan(9.81f / tilt_z) * 2);
				str = df.format(rx) + " " + df.format(tilt_x) + " " + df.format(ang_x) + " " + df.format(ry) + " " + df.format(tilt_z) + " " + df.format(ang_z);
				mm.getInputView().invalidate();
			}
		}

		public void onAccuracyChanged(Sensor event, int res) {}
	};

	protected float getDZ() {
		switch (mm.getPrefsHelper().getTiltDZ()) {
			case 1: return 0.0f;
			case 2: return 0.1f;
			case 3: return 0.25f;
			case 4: return 0.5f;
			case 5: return 1.5f;
			default: return 0.0f;
		}
	}

	protected float getSensitivity() {
		switch (mm.getPrefsHelper().getTiltSensitivity()) {
			case 10: return 1.0f;
			case 9:  return 1.5f;
			case 8:  return 2.0f;
			case 7:  return 2.5f;
			case 6:  return 3.0f;
			case 5:  return 3.5f;
			case 4:  return 4.0f;
			case 3:  return 4.5f;
			case 2:  return 5.0f;
			case 1:  return 5.5f;
			default: return 3.0f;
		}
	}

	protected float getNeutralPos() {
		switch (mm.getPrefsHelper().getTiltVerticalNeutralPos()) {
			case 0:  return 0.0f;
			case 1:  return 1.09f;
			case 2:  return 2.18f;
			case 3:  return 3.27f;
			case 4:  return 4.36f;
			case 5:  return 5.0f;
			case 6:  return 5.45f;
			case 7:  return 6.54f;
			case 8:  return 7.63f;
			case 9:  return 8.72f;
			case 10: return 9.81f;
			default: return 0.0f;
		}
	}
}
