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
import android.content.res.Configuration;
import android.content.res.Resources;
import android.preference.PreferenceManager;

import java.util.Locale;

/**
 * App-wide language support. The user's choice lives in the default shared
 * preferences ("" = follow the device language). Every activity applies the
 * chosen locale in attachBaseContext() via applyLocale() so Android resources
 * resolve against it, the help picks the matching asset folder, and native
 * MAME receives the matching -language option. Unsupported device languages
 * fall back to English (the base resources).
 */
public class LocaleHelper {

	/** Pref key; value is "" (system default) or a code from SUPPORTED. */
	public static final String PREF_LANGUAGE = "PREF_LANGUAGE";

	/** Languages the app ships resources for; base (English) first. */
	private static final String[] SUPPORTED = {"en", "es", "zh", "ja", "pt", "ko"};

	public static String getLanguagePref(Context ctx) {
		return PreferenceManager.getDefaultSharedPreferences(ctx)
				.getString(PREF_LANGUAGE, "");
	}

	/** Register a locale-only override on the activity so its resources use the
	 *  chosen language while STILL tracking runtime config changes (orientation,
	 *  screen size...). A frozen createConfigurationContext() would snapshot the
	 *  orientation and break rotation on activities with android:configChanges.
	 *  Call from attachBaseContext() BEFORE super.attachBaseContext(); a no-op
	 *  when the pref is the system default. */
	public static void applyLocale(android.app.Activity activity, Context base) {
		String lang = getLanguagePref(base);
		if (lang == null || lang.isEmpty())
			return;
		Configuration override = new Configuration();
		override.setLocale(Locale.forLanguageTag(lang));
		/* Only the locale is overridden: preserve the user's font scale (a bare
		 * Configuration defaults it to 1.0, which would override it) and leave
		 * orientation/screen fields UNDEFINED so they follow the live config. */
		override.fontScale = base.getResources().getConfiguration().fontScale;
		activity.applyOverrideConfiguration(override);
	}

	/* The locale the app should honor: the pref if set, else the real device
	 * locale (Resources.getSystem() is immune to our own overrides). */
	private static Locale resolveLocale(Context ctx) {
		String lang = getLanguagePref(ctx);
		if (lang == null || lang.isEmpty())
			return Resources.getSystem().getConfiguration().getLocales().get(0);
		return Locale.forLanguageTag(lang);
	}

	/** Language code the UI is actually showing ("en" when the device
	 *  language has no translation). Drives help + native MAME language. */
	public static String getAppLanguage(Context ctx) {
		String code = resolveLocale(ctx).getLanguage();
		for (String s : SUPPORTED)
			if (s.equals(code))
				return code;
		return "en";
	}

	/** Asset folder holding the localized help ("" = default English). */
	public static String getHelpAssetFolder(Context ctx) {
		String code = getAppLanguage(ctx);
		return code.equals("en") ? "" : "help-" + code;
	}

	/** MAME -language value matching the app language (see language.cpp:
	 *  spaces/parens map to the language/<name>/strings.mo folder). */
	public static String getMameLanguage(Context ctx) {
		String code = getAppLanguage(ctx);
		switch (code) {
			case "es":
				return "Spanish";
			case "zh": {
				/* UI resources only ship Simplified, but MAME has both
				 * scripts: honor Traditional devices (TW/HK/MO or Hant). */
				Locale l = resolveLocale(ctx);
				String script = l.getScript();
				String country = l.getCountry();
				if ("Hant".equals(script) || "TW".equals(country)
						|| "HK".equals(country) || "MO".equals(country))
					return "Chinese (Traditional)";
				return "Chinese (Simplified)";
			}
			case "ja":
				return "Japanese";
			case "pt":
				return "Portuguese (Brazil)";
			case "ko":
				return "Korean";
			default:
				return "English";
		}
	}
}
