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

package com.seleuco.mame4droid.helpers;

import android.content.Context;
import android.os.Build;
import android.os.PowerManager;
import android.util.Log;

import com.seleuco.mame4droid.MAME4droid;

/** ADPF glue, dependency-free and SDK-gated (no-op below): a
 *  PerformanceHintManager hint session (API 31+) over the emu + GL threads plus
 *  a thermal monitor (API 29/30+). Framework types are only used inside guarded
 *  methods and the session is held as Object, so it loads fine on old APIs. */
public class AdpfHelper {

	private static final String TAG = "AdpfHelper";

	private final MAME4droid mm;

	// Kill-switch for the ADPF hint session. The pure_virtual aborts seen in the
	// field were the report()/close() thread race (Session isn't thread-safe),
	// now fixed by synchronizing them. Left as a flag: set false to disable if a
	// residual vendor-HAL crash ever shows up (frame pacing/thermal stay on).
	private static final boolean HINT_SESSION_ENABLED = true;

	// ---- hint session (API 31+) ----
	private volatile Object hintSession; // android.os.PerformanceHintManager.Session
	private boolean sessionTried = false;
	private int emuTid = -1;
	private int glTid = -1;
	private long targetNs = 0;

	// ---- thermal (API 29+/30+) ----
	private PowerManager powerManager;
	private Object thermalListener;  // PowerManager.OnThermalStatusChangedListener
	private volatile int thermalStatus = 0; // THERMAL_STATUS_NONE

	public AdpfHelper(MAME4droid mm) {
		this.mm = mm;
	}

	// =====================================================================
	// Hint session
	// =====================================================================

	/** Register the emulation thread id (call from that thread). */
	public void setEmuThreadTid(int tid) { if (tid != emuTid) { emuTid = tid; dropSession(); } tryCreateSession(); }

	/** Register the GL render thread id (call from that thread). */
	public void setRenderThreadTid(int tid) { if (tid != glTid) { glTid = tid; dropSession(); } tryCreateSession(); }

	/** True if no emu thread is registered (e.g. helper was recreated). */
	public boolean needsEmuTid() { return emuTid <= 0; }

	// a tid changed: close any stale session so it gets rebuilt with fresh tids
	private synchronized void dropSession() {
		if (hintSession != null && Build.VERSION.SDK_INT >= 31) {
			try { ((android.os.PerformanceHintManager.Session) hintSession).close(); }
			catch (Throwable ignored) {}
		}
		hintSession = null;
		sessionTried = false;
	}

	/** Set/refresh the target frame budget from the game's refresh rate. */
	public void setTargetFps(float fps) {
		if (fps <= 1.0f) return;
		long ns = (long) (1_000_000_000.0 / fps);
		if (ns == targetNs) return;
		targetNs = ns;
		if (Build.VERSION.SDK_INT >= 31) {
			if (hintSession == null) tryCreateSession();
			else updateTarget(ns);
		}
	}

	/** Report the actual time this frame took (wall interval is fine). */
	public void reportActualWorkDuration(long ns) {
		if (ns <= 0 || hintSession == null) return;
		if (Build.VERSION.SDK_INT >= 31) report(ns);
	}

	private synchronized void tryCreateSession() {
		if (!HINT_SESSION_ENABLED) return; // never create it: hintSession stays null,
		                                   // so reportActualWorkDuration no-ops via its null check
		if (Build.VERSION.SDK_INT < 31 || hintSession != null || sessionTried) return;
		if (emuTid <= 0 || glTid <= 0 || targetNs <= 0) return;
		createSession();
	}

	private void createSession() {
		sessionTried = true;
		try {
			android.os.PerformanceHintManager phm =
				mm.getSystemService(android.os.PerformanceHintManager.class);
			if (phm == null) return;
			android.os.PerformanceHintManager.Session s =
				phm.createHintSession(new int[]{emuTid, glTid}, targetNs);
			hintSession = s; // null if the platform can't provide one
			Log.d(TAG, "ADPF hint session " + (s != null ? "created" : "unavailable")
				+ " tids=" + emuTid + "," + glTid + " target=" + targetNs + "ns");
		} catch (Throwable t) {
			Log.w(TAG, "ADPF hint session error: " + t);
		}
	}

	// synchronized + null re-check under the lock: the Session is NOT thread-safe
	// and a concurrent close()/dropSession() (onDestroy main thread, GL tid change)
	// would tear down the native session mid-call -> __cxa_pure_virtual abort.
	private synchronized void updateTarget(long ns) {
		if (hintSession == null) return;
		try {
			((android.os.PerformanceHintManager.Session) hintSession).updateTargetWorkDuration(ns);
		} catch (Throwable ignored) {}
	}

	private synchronized void report(long ns) {
		if (hintSession == null) return;
		try {
			((android.os.PerformanceHintManager.Session) hintSession).reportActualWorkDuration(ns);
		} catch (Throwable ignored) {}
	}

	// =====================================================================
	// Thermal
	// =====================================================================

	public void startThermal() {
		if (Build.VERSION.SDK_INT < 29 || thermalListener != null) return;
		try {
			powerManager = (PowerManager) mm.getSystemService(Context.POWER_SERVICE);
			if (powerManager == null) return;
			PowerManager.OnThermalStatusChangedListener l =
				new PowerManager.OnThermalStatusChangedListener() {
					@Override public void onThermalStatusChanged(int status) { thermalStatus = status; }
				};
			powerManager.addThermalStatusListener(l);
			thermalListener = l;
			thermalStatus = powerManager.getCurrentThermalStatus();
		} catch (Throwable t) {
			Log.w(TAG, "thermal monitor unavailable: " + t);
		}
	}

	public void stopThermal() {
		if (powerManager != null && thermalListener != null) {
			try {
				powerManager.removeThermalStatusListener(
					(PowerManager.OnThermalStatusChangedListener) thermalListener);
			} catch (Throwable ignored) {}
		}
		thermalListener = null;
	}

	/** Whether the ADPF hint session is live (telemetry). */
	public boolean isHintSessionActive() { return hintSession != null; }

	/** THERMAL_STATUS_* (0=NONE .. 6=SHUTDOWN); 0 if unknown/unsupported. */
	public int getThermalStatus() { return thermalStatus; }

	/** 0..1 headroom (API 30+), or NaN if unavailable. */
	public float getThermalHeadroom(int forecastSeconds) {
		if (Build.VERSION.SDK_INT < 30 || powerManager == null) return Float.NaN;
		try { return powerManager.getThermalHeadroom(forecastSeconds); }
		catch (Throwable t) { return Float.NaN; }
	}

	// =====================================================================
	// Lifecycle
	// =====================================================================

	/** Release the hint session (thermal is released via stopThermal).
	 *  synchronized so it can't tear down the session while report()/updateTarget()
	 *  (emu thread) is mid-call - see the race note on report(). */
	public synchronized void close() {
		stopThermal();
		if (hintSession != null && Build.VERSION.SDK_INT >= 31) {
			try { ((android.os.PerformanceHintManager.Session) hintSession).close(); }
			catch (Throwable ignored) {}
		}
		hintSession = null;
		sessionTried = false;
		emuTid = glTid = -1;
		targetNs = 0;
	}
}
