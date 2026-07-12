/*
 * This file is part of MAME4droid.
 *
 * Copyright (C) 2024 David Valdeita (Seleuco)
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

package com.seleuco.mame4droid;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

public class WebHelpActivity extends Activity {

	WebView lWebView = null;

	@Override
	protected void attachBaseContext(android.content.Context newBase) {
		com.seleuco.mame4droid.helpers.LocaleHelper.applyLocale(this, newBase);
		super.attachBaseContext(newBase);
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {

		super.onCreate(savedInstanceState);
		String path = null;//"/storage/emulated/0/ROMs/MAME4droid/";
		setContentView(R.layout.webhelp);
		Bundle extras = getIntent().getExtras();
		if (extras != null) {
			path = extras.getString("INSTALLATION_PATH");
		}
		lWebView = (WebView) this.findViewById(R.id.webView);
		WebSettings webSettings = lWebView.getSettings();
		webSettings.setJavaScriptEnabled(true);
		//webSettings.setBuiltInZoomControls(true);
		//lWebView.setWebViewClient(new WebViewClient());
		lWebView.setBackgroundColor(Color.DKGRAY);
		if (!path.endsWith("/"))
			path += "/";

		//lWebView.loadUrl("file:///" +  path +"help/index.htm");
		/* Use the localized help only if that folder is actually bundled;
		 * otherwise fall back to the English help at the assets root, so a
		 * language without translated help still opens (English) instead of
		 * a broken page. */
		String helpFolder = com.seleuco.mame4droid.helpers.LocaleHelper.getHelpAssetFolder(this);
		if (!helpFolder.isEmpty() && !assetExists(helpFolder + "/index.htm"))
			helpFolder = "";
		lWebView.loadUrl("file:///android_asset/"
				+ (helpFolder.isEmpty() ? "" : helpFolder + "/") + "index.htm");

		// attempt to fix FileUriExposedException
		//https://stackoverflow.com/questions/40560604/navigating-asset-based-html-files-in-webview-on-nougat
        /*
        WebViewClient client = new WebViewClient(){
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                return false;
            }
        };
        lWebView.setWebViewClient(client);
        */
		lWebView.setWebViewClient(new WebViewClient() {
			@Override
			public boolean shouldOverrideUrlLoading(WebView webView, WebResourceRequest webResourceRequest) {
				if (webResourceRequest.getUrl().getScheme().equals("file")) {
					webView.loadUrl(webResourceRequest.getUrl().toString());
				} else {
					// If the URI is not pointing to a local file, open with an ACTION_VIEW Intent
					webView.getContext().startActivity(new Intent(Intent.ACTION_VIEW, webResourceRequest.getUrl()));
				}
				return true; // in both cases we handle the link manually
			}

			/* Per-asset fallback: a page or the shared stylesheet missing from
			 * the localized help folder is served from the English root, so a
			 * partially translated language still renders correctly. */
			@Override
			public android.webkit.WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
				android.net.Uri uri = request.getUrl();
				if (!"file".equals(uri.getScheme())) return null;
				String p = uri.getPath();
				final String base = "/android_asset/";
				int i = (p == null) ? -1 : p.indexOf(base);
				if (i < 0) return null;
				String rel = p.substring(i + base.length());
				if (assetExists(rel)) return null; // real localized asset: let it load
				int slash = rel.indexOf('/');
				if (slash < 0 || !rel.substring(0, slash).startsWith("help-")) return null;
				String english = rel.substring(slash + 1); // drop help-xx/ prefix
				try {
					String mime = english.endsWith(".css") ? "text/css"
							: english.endsWith(".htm") || english.endsWith(".html") ? "text/html"
							: "application/octet-stream";
					return new android.webkit.WebResourceResponse(mime, "utf-8", getAssets().open(english));
				} catch (java.io.IOException e) {
					return null;
				}
			}
		});

		lWebView.requestFocus();
	}


	/* True if the given path exists under the APK assets (used to decide
	 * whether a localized help folder was actually bundled). */
	private boolean assetExists(String assetPath) {
		try (java.io.InputStream is = getAssets().open(assetPath)) {
			return true;
		} catch (java.io.IOException e) {
			return false;
		}
	}

	public void onBackPressed() {

		if (this.lWebView.canGoBack())
			this.lWebView.goBack();
		else
			super.onBackPressed();

		lWebView.requestFocus();
	}

}
