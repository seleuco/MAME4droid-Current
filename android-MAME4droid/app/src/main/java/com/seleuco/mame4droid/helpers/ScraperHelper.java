/*
 * This file is part of MAME4droid.
 *
 * Copyright (C) 2023 David Valdeita (Seleuco)
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

import android.util.Log;

import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.scrape.ADBScraper;
import com.seleuco.mame4droid.scrape.IScraper;
import com.seleuco.mame4droid.scrape.ScrapeException;
import com.seleuco.mame4droid.widgets.WarnWidget;
import android.graphics.Color;

import java.io.*;
import java.util.ArrayList;

public class ScraperHelper implements Runnable {

    protected MAME4droid mm = null;

    private static final String TAG = "SCRAPPER";

    public static boolean isRunning = false;
    public static boolean isPaused = false;
	public static boolean isStopped = false;
	public static boolean isScraping = false;

	private static int current = 0;

	private static IScraper scraper = null;
    private static Thread scraperThread;
    private static final ArrayList<String> names = new ArrayList<>();

	public static void reset(){
		if(scraper!=null)
			scraper.reset();
	}

    public ScraperHelper(MAME4droid value) {
        mm = value;
		if(scraper!=null){
			scraper.setMAME4droid(value);
		}
    }

    public void initMediaScrap() {

		if(!mm.getPrefsHelper().isScrapingIcons() &&
			!mm.getPrefsHelper().isScrapingSnapshots() &&
			!mm.getPrefsHelper().isScrapingAll() ) {
			Log.d(TAG, "There's nothing to scrape");
			return;
		}

        if (!isRunning) {

			if(scraper==null) {
				scraper = new ADBScraper(mm.getPrefsHelper().getInstallationDIR(),mm);
			}
            isRunning = true;
			isPaused = false;
			isStopped = false;
			isScraping = false;
			current =0;
            ArrayList<String> fileNames = mm.getSAFHelper().getRomsFileNames();
            for (String name : fileNames) {
                if (name.toLowerCase().endsWith(".7z") || name.toLowerCase().endsWith(".zip"))
                    names.add(name.substring(0, name.indexOf(".")));
            }
            scraperThread = new Thread(this);
            scraperThread.start();
        }
    }

    @Override
    public void run() {
        Log.d(TAG, "Scraping starts");
        for (String name : names) {

			File f = new File(mm.getPrefsHelper().getInstallationDIR());

			long freeGB = f.getFreeSpace() / (1024*1024*1024);

			Log.d(TAG, "free space: " + freeGB);

			if(freeGB <= 1)
			{
				new WarnWidget.WarnWidgetHelper(mm, mm.getString(com.seleuco.mame4droid.R.string.scraping_no_space), 3, Color.RED, false);
				ScraperHelper.isStopped = true;
				break;
			}

			boolean scrapping = false;

			try {
				scrapping = scraper.scrape(name, current);
			}catch (ScrapeException e) {
				new WarnWidget.WarnWidgetHelper(mm, mm.getString(com.seleuco.mame4droid.R.string.scraping_error, e.getMessage()), 3, Color.RED, false);
				break;
			}

			if(!ScraperHelper.isScraping && scrapping){
				ScraperHelper.isScraping = true;
				new WarnWidget.WarnWidgetHelper(mm, mm.getString(com.seleuco.mame4droid.R.string.scraping_running), 3, Color.GREEN, true);
			}

			current++;

            synchronized(this) {
                if (isPaused) {
                    try {
						Log.d(TAG, "Scraping paused");
                        wait();
						Log.d(TAG, "Scraping resumed");
                    } catch (InterruptedException ignored) {}
                }
            }

			if(isStopped){
				Log.d(TAG, "Scraping stopped");
				break;
			}
        }
        isRunning = false;

		if(isScraping && !isStopped){
			new WarnWidget.WarnWidgetHelper(mm, mm.getString(com.seleuco.mame4droid.R.string.scraping_ends), 5, Color.GREEN, true);
		}
        Log.d(TAG, "Scraping ends");
    }

    synchronized public void pause() {
		Log.d(TAG, "Scraping calling pause");
        if(!isPaused && isRunning){
            isPaused = true;
        }
    }

    synchronized public void resume() {
		Log.d(TAG, "Scraping calling resume");
        if(isPaused && isRunning){
			isPaused = false;
            notify();
        }
    }

	synchronized public void stop() {
		Log.d(TAG, "Scraping calling stop");
		if(isRunning){
			isStopped = true;
			notify();
		}
	}

}
