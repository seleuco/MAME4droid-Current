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

package com.seleuco.mame4droid.prefs;

import com.seleuco.mame4droid.input.GameController;
import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.view.Gravity;

public class KeySelect extends Activity {

	protected int emulatorInputIndex;

	@Override
	public void onCreate(Bundle icicle) {
		super.onCreate(icicle);

		getWindow().setFlags(WindowManager.LayoutParams.FLAG_BLUR_BEHIND,
			WindowManager.LayoutParams.FLAG_BLUR_BEHIND);

		emulatorInputIndex = getIntent().getIntExtra("emulatorInputIndex", 0);
		setTitle("Press button for \"" + ListKeys.emulatorInputLabels[emulatorInputIndex] + "\"");

		int dp16 = (int) (16 * getResources().getDisplayMetrics().density);
		int dp300 = (int) (300 * getResources().getDisplayMetrics().density); // Ancho máximo sugerido

		LinearLayout root = new LinearLayout(this);
		root.setOrientation(LinearLayout.VERTICAL);
		root.setGravity(Gravity.CENTER);
		root.setPadding(dp16, dp16, dp16, dp16);

		LinearLayout dialogBox = new LinearLayout(this);
		dialogBox.setOrientation(LinearLayout.VERTICAL);

		LinearLayout.LayoutParams dialogParams = new LinearLayout.LayoutParams(
			dp300, ViewGroup.LayoutParams.WRAP_CONTENT);
		dialogBox.setLayoutParams(dialogParams);

		Button cancelButton = new Button(this);
		cancelButton.setText("Cancel");
		cancelButton.setOnClickListener(new View.OnClickListener() {
			public void onClick(View v) {
				setResult(RESULT_CANCELED, new Intent());
				finish();
			}
		});

		Button clearButton = new Button(this);
		clearButton.setText("Clear");
		clearButton.setOnClickListener(new View.OnClickListener() {
			public void onClick(View v) {
				setResult(RESULT_OK, new Intent().putExtra("androidKeyCode", -1));
				finish();
			}
		});

		TextView helpText = new TextView(this);
		helpText.setText("\nWaiting for input...\n");
		helpText.setGravity(Gravity.CENTER);
		helpText.setTextAppearance(this, android.R.style.TextAppearance_Medium);

		View keyCapturer = new View(this) {
			@Override
			public boolean onKeyDown(int keyCode, KeyEvent event) {
				if (emulatorInputIndex != 12 && keyCode == KeyEvent.KEYCODE_BACK) return false;
				if (emulatorInputIndex != 13 && keyCode == KeyEvent.KEYCODE_MENU) return false;

				setResult(RESULT_OK, new Intent()
					.putExtra("androidKeyCode", keyCode)
					.putExtra("androidGamePadID", GameController.getPersistentDeviceId(event.getDevice())));

				finish();
				return true;
			}
		};
		keyCapturer.setFocusable(true);
		keyCapturer.setFocusableInTouchMode(true);
		keyCapturer.requestFocus();

		LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
			ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
		lp.setMargins(0, 0, 0, dp16);

		dialogBox.addView(cancelButton, lp);
		dialogBox.addView(clearButton, lp);
		dialogBox.addView(helpText);
		dialogBox.addView(keyCapturer, new LinearLayout.LayoutParams(1, 1)); // Casi invisible

		root.addView(dialogBox);

		setContentView(root, new ViewGroup.LayoutParams(
			ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
	}
}
