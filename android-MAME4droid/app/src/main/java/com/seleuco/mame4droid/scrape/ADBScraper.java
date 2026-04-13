/*
 * This file is part of MAME4droid.
 *
 * Copyright (C) 2025 David Valdeita (Seleuco)
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

package com.seleuco.mame4droid.scrape;

import android.util.Log;

import com.seleuco.mame4droid.MAME4droid;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Enumeration;
import java.util.Properties;

/**
 * Implements the IScraper interface to download and save game media and data
 * from the Arcade Database (ADB) at adb.arcadeitalia.net. This class handles
 * HTTP requests, JSON parsing (via string manipulation), and file I/O to
 * persist the scraped content.
 */
public class ADBScraper implements IScraper {

	private static final String TAG = "SCRAPPER-ADB";

	private static final int TIMEOUT = 5000;

	// Constants for web service URLs, user agent, and property keys.
	private static final String USER_AGENT = "MAME4droid/1.0";
	private static final String ADB_STATUS_ADB_URL = "https://adb.arcadeitalia.net/service_scraper.php?ajax=download_status";
	private static final String ADB_QUERY_MAME_URL = "https://adb.arcadeitalia.net/service_scraper.php?ajax=query_mame";
	private static final String ADB_QUERY_FULL_URL = "https://adb.arcadeitalia.net/service_scraper.php?ajax=query_mame_media";

	// File types
	private static final String FILE_TYPE_PNG = "png";
	private static final String FILE_TYPE_ICO = "ico";

	// Property keys for different media types stored in the properties file.
	private static final String PROPERTIE_INGAME = "ingame";
	private static final String PROPERTIE_ICON = "icon";
	private static final String PROPERTIE_FLYER = "flyer";
	private static final String PROPERTIE_MARQUEE = "marquee";
	private static final String PROPERTIE_TITLE = "title";
	private static final String PROPERTIE_CABINET = "cabinet";
	private static final String PROPERTIE_ARTPREVIEW = "artpreview";
	private static final String PROPERTIE_BOSS = "boss";
	private static final String PROPERTIE_CPANEL = "cpanel";
	private static final String PROPERTIE_HOWTO = "howto";
	private static final String PROPERTIE_LOGO = "logo";
	private static final String PROPERTIE_PCB = "pcb";
	private static final String PROPERTIE_SCORE = "score";
	private static final String PROPERTIE_SELECT = "select";
	private static final String PROPERTIE_DECAL = "decal";
	private static final String PROPERTIE_VERSUS = "versus";
	private static final String PROPERTIE_GAMEOVER = "gameover";
	private static final String PROPERTIE_END = "end";
	private static final String PROPERTIE_WARNING = "warning";
	//private static final String PROPERTIE_BOX = "box";

	// Directory names for storing downloaded media.
	private static final String DIR_INGAME = "snap";
	private static final String DIR_ICON = "icons";
	private static final String DIR_CPANEL = "cpanel";
	private static final String DIR_CABINET = "cabinets";
	private static final String DIR_MARQUEE = "marquees";
	private static final String DIR_PCB = "pcb";
	private static final String DIR_FLYER = "flyers";
	private static final String DIR_TITLE = "titles";
	private static final String DIR_END = "ends";
	private static final String DIR_BOSS = "bosses";
	private static final String DIR_ARTPREVIEW = "artpreview";
	private static final String DIR_SELECT = "select";
	private static final String DIR_GAMEOVER = "gameover";
	private static final String DIR_HOWTO = "howto";
	private static final String DIR_LOGO = "logo";
	private static final String DIR_SCORE = "scores";
	private static final String DIR_VERSUS = "versus";
	private static final String DIR_DECAL = "covers";
	//private static final String DIR_BOX = "box";

	private static final String DIR_WARNING = "warning";

	private static final String DIR_PROPERTIES = "scrape";

	private final String scrapeDir;
	private Boolean scraping = false;

	private MAME4droid mm = null;

	/**
	 * Constructs the ADBScraper.
	 *
	 * @param dir The base directory where scraped files will be stored.
	 * @param mm A reference to the MAME4droid application.
	 */
	public ADBScraper(String dir, MAME4droid mm){
		scrapeDir = dir;
		this.mm = mm;
	}

	@Override
	public void setMAME4droid(MAME4droid mm) {
		this.mm = mm;
	}

	/**
	 * Determines if a specific media type for a ROM needs to be downloaded.
	 * It checks if the file exists in the properties file and if the user preferences
	 * allow for the download of that media type.
	 *
	 * @param p The Properties object containing the status of downloaded files.
	 * @param k The key representing the media type (e.g., "ingame", "icon").
	 * @return true if the file needs to be downloaded, false otherwise.
	 */
	protected boolean needsDownload(Properties p, String k){
		// Checks user preferences and download status in properties file.
		if(PROPERTIE_ICON.equals(k) && !mm.getPrefsHelper().isScrapingIcons())
			return false;
		if(PROPERTIE_INGAME.equals(k) && !mm.getPrefsHelper().isScrapingSnapshots())
			return false;
		if(!PROPERTIE_ICON.equals(k) && !PROPERTIE_INGAME.equals(k) && !mm.getPrefsHelper().isScrapingAll())
			return false;

		if(p.get(k)==null) return true;
		return !Boolean.parseBoolean((String) p.get(k));
	}

	@Override
	public boolean scrape(String rom_name, int current) throws ScrapeException{
		try {

			Properties props = new Properties();
			String propsDir = scrapeDir + DIR_PROPERTIES;

			Files.createDirectories(Paths.get(propsDir));

			File propsFile = new File(propsDir + File.separator + rom_name + ".properties");

			Log.d(TAG, "processing: " + rom_name+ " num:"+current);

			boolean skip = false;

			// Check if a properties file already exists and if any media needs to be downloaded.
			if (propsFile.exists()) {
				FileInputStream in = new FileInputStream(propsFile);
				props.load(in);
				in.close();
				skip = true;

				if(skip) skip = !needsDownload(props, PROPERTIE_INGAME);
				if(skip) skip = !needsDownload(props, PROPERTIE_ICON);
				if(skip) skip = !needsDownload(props, PROPERTIE_CPANEL);
				if(skip) skip = !needsDownload(props, PROPERTIE_CABINET);
				if(skip) skip = !needsDownload(props, PROPERTIE_MARQUEE);
				if(skip) skip = !needsDownload(props, PROPERTIE_PCB);
				if(skip) skip = !needsDownload(props, PROPERTIE_FLYER);
				if(skip) skip = !needsDownload(props, PROPERTIE_TITLE);
				if(skip) skip = !needsDownload(props, PROPERTIE_END);
				if(skip) skip = !needsDownload(props, PROPERTIE_BOSS);
				if(skip) skip = !needsDownload(props, PROPERTIE_ARTPREVIEW);
				if(skip) skip = !needsDownload(props, PROPERTIE_SELECT);
				if(skip) skip = !needsDownload(props, PROPERTIE_GAMEOVER);
				if(skip) skip = !needsDownload(props, PROPERTIE_HOWTO);
				if(skip) skip = !needsDownload(props, PROPERTIE_LOGO);
				if(skip) skip = !needsDownload(props, PROPERTIE_SCORE);
				if(skip) skip = !needsDownload(props, PROPERTIE_VERSUS);
			}

			if (skip) {
				Log.d(TAG, "skipping: " + rom_name);
				return false;
			}

			// Check ADB availability before starting the scrape process.
			if(!scraping) {
				String json_status = getJSON(ADB_STATUS_ADB_URL, TIMEOUT * 2);
				if (json_status == null)
					throw new ScrapeException("ADB is not available!");
			}
			scraping = true;

			Log.d(TAG, "scraping: " + rom_name);

			String json_query = mm.getPrefsHelper().isScrapingAll() ? ADB_QUERY_FULL_URL : ADB_QUERY_MAME_URL;
			String json = getJSON(json_query + "&game_name=" + rom_name, TIMEOUT);
			Log.d(TAG, "json: " + json);

			if (json == null) {
				Log.d(TAG, "Not json: " + rom_name);
				return true;
			}

			if (!isEmpty(json)) {
				// Get image URLs from the JSON response and download them.
				String url_icon = getImageURL("url_icon", json);
				String url_image_ingame = getImageURL("url_image_ingame", json);

				if (needsDownload(props, PROPERTIE_INGAME))
					props.put(PROPERTIE_INGAME, download(rom_name, FILE_TYPE_PNG, url_image_ingame, TIMEOUT, DIR_INGAME) + "");
				if (needsDownload(props, PROPERTIE_ICON))
					props.put(PROPERTIE_ICON, download(rom_name, FILE_TYPE_ICO, url_icon, TIMEOUT, DIR_ICON) + "");

				// Download additional media if the "scrape all" preference is enabled.
				if(mm.getPrefsHelper().isScrapingAll()){
					String url_image_cpanel = getImageURL("url_image_cpanel", json);
					String url_image_cabinet = getImageURL("url_image_cabinet", json);
					String url_image_marquee = getImageURL("url_image_marquee", json);
					String url_image_pcb = getImageURL("url_image_pcb", json);
					String url_image_flyer = getImageURL("url_image_flyer", json);
					String url_image_title = getImageURL("url_image_title", json);
					String url_image_end = getImageURL("url_image_end", json);
					String url_image_boss = getImageURL("url_image_boss", json);
					String url_image_artpreview = getImageURL("url_image_artwork_preview", json);
					String url_image_select = getImageURL("url_image_select", json);
					String url_image_gameover = getImageURL("url_image_gameover", json);
					String url_image_howto = getImageURL("url_image_howto", json);
					String url_image_logo = getImageURL("url_image_logo", json);
					String url_image_score = getImageURL("url_image_score", json);
					String url_image_versus = getImageURL("url_image_versus", json);

					//String url_image_box = getImageURL("url_image_box", json);
					//String url_image_decal = getImageURL("url_image_decal", json);
					//String url_image_warning = getImageURL("url_image_warning", json);

					if (needsDownload(props, PROPERTIE_CPANEL))
						props.put(PROPERTIE_CPANEL, download(rom_name, FILE_TYPE_PNG, url_image_cpanel, TIMEOUT, DIR_CPANEL) + "");
					if (needsDownload(props, PROPERTIE_CABINET))
						props.put(PROPERTIE_CABINET, download(rom_name, FILE_TYPE_PNG, url_image_cabinet, TIMEOUT, DIR_CABINET) + "");
					if (needsDownload(props, PROPERTIE_MARQUEE))
						props.put(PROPERTIE_MARQUEE, download(rom_name, FILE_TYPE_PNG, url_image_marquee, TIMEOUT, DIR_MARQUEE) + "");
					if (needsDownload(props, PROPERTIE_PCB))
						props.put(PROPERTIE_PCB, download(rom_name, FILE_TYPE_PNG, url_image_pcb, TIMEOUT, DIR_PCB) + "");
					if (needsDownload(props, PROPERTIE_FLYER))
						props.put(PROPERTIE_FLYER, download(rom_name, FILE_TYPE_PNG, url_image_flyer, TIMEOUT, DIR_FLYER) + "");
					if (needsDownload(props, PROPERTIE_TITLE))
						props.put(PROPERTIE_TITLE, download(rom_name, FILE_TYPE_PNG, url_image_title, TIMEOUT, DIR_TITLE) + "");
					if (needsDownload(props, PROPERTIE_END))
						props.put(PROPERTIE_END, download(rom_name, FILE_TYPE_PNG, url_image_end, TIMEOUT, DIR_END) + "");
					if (needsDownload(props, PROPERTIE_BOSS))
						props.put(PROPERTIE_BOSS, download(rom_name, FILE_TYPE_PNG, url_image_boss, TIMEOUT, DIR_BOSS) + "");
					if (needsDownload(props, PROPERTIE_ARTPREVIEW))
						props.put(PROPERTIE_ARTPREVIEW, download(rom_name, FILE_TYPE_PNG, url_image_artpreview, TIMEOUT, DIR_ARTPREVIEW) + "");
					if (needsDownload(props, PROPERTIE_SELECT))
						props.put(PROPERTIE_SELECT, download(rom_name, FILE_TYPE_PNG, url_image_select, TIMEOUT, DIR_SELECT) + "");
					if (needsDownload(props, PROPERTIE_GAMEOVER))
						props.put(PROPERTIE_GAMEOVER, download(rom_name, FILE_TYPE_PNG, url_image_gameover, TIMEOUT, DIR_GAMEOVER) + "");
					if (needsDownload(props, PROPERTIE_HOWTO))
						props.put(PROPERTIE_HOWTO, download(rom_name, FILE_TYPE_PNG, url_image_howto, TIMEOUT, DIR_HOWTO) + "");
					if (needsDownload(props, PROPERTIE_LOGO))
						props.put(PROPERTIE_LOGO, download(rom_name, FILE_TYPE_PNG, url_image_logo, TIMEOUT, DIR_LOGO) + "");
					if (needsDownload(props, PROPERTIE_SCORE))
						props.put(PROPERTIE_SCORE, download(rom_name, FILE_TYPE_PNG, url_image_score, TIMEOUT, DIR_SCORE) + "");
					if (needsDownload(props, PROPERTIE_VERSUS))
						props.put(PROPERTIE_VERSUS, download(rom_name, FILE_TYPE_PNG, url_image_versus, TIMEOUT, DIR_VERSUS) + "");
				}

			} else {
				Log.d(TAG, "Not data: " + rom_name);
			}

			// Save the properties file.
			FileOutputStream out = new FileOutputStream(propsFile);
			props.store(out, null);
			out.close();

		} catch (IOException ex) {
			Log.e(TAG,ex.toString());
		}

		return true;
	}

	@Override
	public boolean reset() {

		/*
		Log.d(TAG, "Deleting properties...");
		String propsDir = scrapeDir + DIR_PROPERTIES;
		File f = new File(propsDir);
		File[] contents = f.listFiles();
		if (contents != null) {
			for (File f2 : contents) {
				f2.delete();
			}
		}
		*/
		return true;
	}

	/**
	 * Extracts an image URL from a JSON string.
	 * NOTE: This is a brittle method for parsing JSON.
	 *
	 * @param imageType The key for the image URL in the JSON.
	 * @param json The JSON string to parse.
	 * @return The extracted URL, or null if not found.
	 */
	protected String getImageURL(String imageType, String json) {
		String url = null;
		String token = "\"" + imageType + "\":";
		int i = json.indexOf(token);
		if (i == -1) return url;
		i += token.length();
		i = json.indexOf("\"", i);
		int j = json.indexOf("\"", i + 1);
		if (j == -1) return url;
		url = json.substring(i + 1, j);
		if (url.trim().length() == 0)
			return null;
		url = url.replace("\\", "");

		// Appends resize parameters based on user preferences.
		if (url.endsWith("resize=0")) {
			if (mm.getPrefsHelper().isScrapingResize())
				url = url.replace("resize=0", "resize=300");
		} else {
			if (mm.getPrefsHelper().isScrapingResize()) {
				url += "&resize=300";
			} else {
				url += "&resize=0";
			}
		}
		return url;
	}

	/**
	 * Checks if the JSON response contains an empty result.
	 *
	 * @param json The JSON string to check.
	 * @return true if the "result" field is an empty array, false otherwise.
	 */
	protected boolean isEmpty(String json) {
		String data = null;
		String token = "\"result\":";
		int i = json.indexOf(token);
		if (i == -1) return false;
		i += token.length();
		i = json.indexOf("[", i);
		int j = json.indexOf("]", i + 1);
		if (j == -1) return false;
		data = json.substring(i + 1, j);
		return data.isEmpty();
	}

	/**
	 * Makes an HTTP GET request to a specified URL and returns the JSON response.
	 *
	 * @param url The URL to connect to.
	 * @param timeout The connection timeout in milliseconds.
	 * @return The JSON response as a String, or null on error.
	 */
	protected String getJSON(String url, int timeout) {
		HttpURLConnection c = null;
		try {
			URL u = new URL(url);
			c = (HttpURLConnection) u.openConnection();
			c.setRequestProperty("User-Agent", USER_AGENT);
			c.setRequestMethod("GET");
			c.setRequestProperty("Content-length", "0");
			c.setUseCaches(false);
			c.setAllowUserInteraction(false);
			c.setConnectTimeout(timeout);
			c.setReadTimeout(timeout);
			c.connect();
			int status = c.getResponseCode();

			switch (status) {
				//if error with ADB HTTP/1.0 503 Service Unavailable message
				case 200:
				case 201:
					BufferedReader br = new BufferedReader(new InputStreamReader(c.getInputStream()));
					StringBuilder sb = new StringBuilder();
					String line;
					while ((line = br.readLine()) != null) {
						sb.append(line).append("\n");
					}
					br.close();
					return sb.toString();
			}
		} catch (IOException ex) {
			Log.e(TAG,ex.toString());
		} finally {
			if (c != null) {
				try {
					c.disconnect();
				} catch (Exception ignored) {}
			}
		}
		return null;
	}

	/**
	 * Downloads a file from a URL and saves it to the specified directory.
	 *
	 * @param name The base name for the file.
	 * @param fileType The file extension (e.g., "png", "ico").
	 * @param url The URL of the file to download.
	 * @param timeout The connection timeout.
	 * @param folder The sub-directory to save the file in.
	 * @return true if the download was successful, false otherwise.
	 */
	protected boolean download(String name, String fileType, String url, int timeout, String folder) {
		String dir = scrapeDir + folder;
		String fileName = name + "." + fileType;
		File file = new File(dir + File.separator + fileName);
		if(url == null) {
			Log.d(TAG, "Theres not url image: " + fileName +" ("+folder+")");
			return true;
		}
		if(file.exists()){
			Log.d(TAG, "Already exists: " + fileName);
			return true;
		}
		Log.d(TAG, "Downloading: " + url);
		HttpURLConnection c = null;
		try {
			URL u = new URL(url);
			c = (HttpURLConnection) u.openConnection();
			c.setRequestProperty("User-Agent", USER_AGENT);
			c.setRequestMethod("GET");
			c.setRequestProperty("Content-length", "0");
			c.setUseCaches(false);
			c.setAllowUserInteraction(false);
			c.setConnectTimeout(timeout);
			c.setReadTimeout(timeout);
			c.connect();
			int status = c.getResponseCode();

			switch (status) {
				case 200:
				case 201:
                   /*
                   	String raw = c.getHeaderField("Content-Disposition");
                    if(raw != null && raw.indexOf("=") != -1) {
                        fileName = raw.split("=")[1]; //getting value after '='
                        if(fileName.startsWith("\"") && fileName.endsWith("\""))
                            fileName = fileName.substring(1,fileName.length()-1);
                    }
                    if(fileName==null)
                    */

					InputStream input = c.getInputStream();
					byte[] buffer = new byte[4096];
					int n;
					Files.createDirectories(Paths.get(dir));
					OutputStream output = new FileOutputStream(file);
					while ((n = input.read(buffer)) != -1) {
						output.write(buffer, 0, n);
					}
					output.close();
					return true;
			}

		} catch (IOException ex) {
			Log.e(TAG,ex.toString());
		} finally {
			if (c != null) {
				try {
					c.disconnect();
				} catch (Exception ignored) {}
			}
		}
		return false;
	}
}
