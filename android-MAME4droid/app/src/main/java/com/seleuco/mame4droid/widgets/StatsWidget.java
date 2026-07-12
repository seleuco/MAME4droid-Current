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

package com.seleuco.mame4droid.widgets;

import android.graphics.Color;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.FrameLayout;
import android.widget.TextView;

import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.R;

/* Small, non-intrusive bottom-center overlay showing NetPlay connection
 * quality (mode/ping/delay), refreshed by a native push every ~2s while a
 * session is active (see the "STATS:" prefix handled in Emulator.netplayWarn).
 * Single persistent TextView, transparent background, updated in place --
 * unlike WarnWidget this never auto-expires, only hide() removes it.
 * NOTE: MAME4droid.onConfigurationChanged() rebuilds the whole content view
 * (setContentView + re-inflate) on every rotation, so R.id.EmulatorFrame is
 * a NEW instance afterwards -- the cached textView must be re-parented (or
 * recreated) rather than assumed still attached, or it silently stops
 * being reachable/visible after the very next STATS push post-rotation. */
public class StatsWidget {

	private static TextView textView = null;

	/* payload = "<COLOR>:<text>", COLOR one of GREEN/YELLOW/RED. */
	public static void update(final MAME4droid mm, String payload) {
		int sep = payload.indexOf(':');
		if (sep < 0) return;
		final String colorName = payload.substring(0, sep);
		final String text = payload.substring(sep + 1);
		final int color;
		if ("RED".equals(colorName))
			color = Color.RED;
		else if ("YELLOW".equals(colorName))
			color = Color.YELLOW;
		else
			color = Color.GREEN;

		mm.runOnUiThread(new Runnable() {
			public void run() {
				try {
					FrameLayout frame = mm.findViewById(R.id.EmulatorFrame);

					if (textView == null || textView.getParent() != frame) {
						if (textView != null && textView.getParent() instanceof FrameLayout)
							((FrameLayout) textView.getParent()).removeView(textView);

						textView = new TextView(mm);
						FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
								FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT);
						params.gravity = Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL;

						DisplayMetrics dm = mm.getResources().getDisplayMetrics();
						float px = 6 * ((float) dm.densityDpi / DisplayMetrics.DENSITY_DEFAULT);
						params.setMargins(0, 0, 0, (int) px);

						textView.setLayoutParams(params);
						/* Proportional to the screen short edge (not sp): a fixed sp/dp
						 * looks big on small screens, tiny on large, and sp also swings
						 * with font-scale.  Clamped px = stable across density/size/font. */
						int shortEdge = Math.min(dm.widthPixels, dm.heightPixels);
						float textPx = Math.max(14f, Math.min(44f, shortEdge * 0.025f));
						textView.setTextSize(TypedValue.COMPLEX_UNIT_PX, textPx);
						textView.setBackgroundColor(Color.TRANSPARENT);
						textView.setShadowLayer(3, 1, 1, Color.BLACK);

						frame.addView(textView);
					}
					textView.setTextColor(color);
					textView.setText(text);
				} catch (Throwable ignored) {}
			}
		});
	}

	public static void hide(final MAME4droid mm) {
		mm.runOnUiThread(new Runnable() {
			public void run() {
				try {
					if (textView != null) {
						if (textView.getParent() instanceof FrameLayout)
							((FrameLayout) textView.getParent()).removeView(textView);
						textView = null;
					}
				} catch (Throwable ignored) {}
			}
		});
	}
}
