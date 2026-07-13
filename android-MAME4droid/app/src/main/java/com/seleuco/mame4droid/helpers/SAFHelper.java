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

package com.seleuco.mame4droid.helpers;

import android.content.Context;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.DocumentsContract;
import android.util.Log;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.widgets.WarnWidget;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

// Inner class to represent a file or directory entry.
class DirEntry {
	String name;
	String docId;
	long size;
	long modified;
	boolean isDir;
}

// Inner class to manage the state of an opened directory for iteration.
class DirEntries {
	private static final AtomicInteger lastId = new AtomicInteger(1);
	final int id;
	int dirEntIdx = 0; // Current index for reading entries.
	ArrayList<DirEntry> dirEntries = null;

	DirEntries() {
		this.id = lastId.getAndIncrement();
	}
}

/**
 * Abstracts Android's Storage Access Framework (SAF) as a file-system-like
 * interface over a URI-based directory tree, cached in memory (full scan,
 * persisted, or lazy on-demand). Expensive calls should run off the UI thread.
 */
public class SAFHelper {

	private static final String TAG = "SAFHelper";

	private static final String CACHE_FILE_NAME = "saf_cache.bin";
	private static final String JOURNAL_FILE_NAME = "saf_cache.dirty";
	private static final int CACHE_MAGIC = 0x4D344443; // "M4DC"
	private static final int CACHE_VERSION = 2;
	private static final int SCAN_DEPTH_LIMIT = 6;
	private static final int SCAN_THREADS = 4;

	private static Uri rootUri = null;
	// Concurrent: the full scan fills them from a small thread pool; afterwards
	// only the emulator thread mutates them (lazy listing, file creation).
	private static Map<String, String> fileIDs = null;
	private static Map<String, ArrayList<DirEntry>> dirFiles = null;

	// Whether the maps come from a complete scan or a valid cache file (only then
	// may they be persisted). Lazy mode keeps partial maps and never persists.
	private static boolean cacheComplete = false;
	// In-memory maps changed after the scan/load (files or dirs created by MAME).
	private static boolean cacheDirty = false;
	// Directories MAME wrote into this session (e.g. "/sta/") - NOT their
	// ancestors: re-querying the parent (often the root, thousands of ROMs)
	// would be wasted. Refreshed and saved once, when the emulator exits.
	private static final Set<String> dirtyDirs = ConcurrentHashMap.newKeySet();
	// The root directory's own mtime at the moment the cache was persisted; the
	// startup freshness check compares against it with a single-row query (the
	// root often holds thousands of ROMs, so listing its children is expensive).
	private static long cachedRootMtime = 0;
	private static boolean staleWarned = false;

	private final MAME4droid mm;
	private final Map<Integer, DirEntries> openDirs = new HashMap<>();
	private WarnWidget pw = null;

	public SAFHelper(MAME4droid value) {
		this.mm = value;
	}

	/**
	 * Sets the root URI for all SAF operations.
	 *
	 * @param uriStr The string representation of the tree URI granted by the user.
	 */
	public void setURI(String uriStr) {
		Log.d(TAG, "Setting SAF URI: " + uriStr);
		if (uriStr == null) {
			rootUri = null;
		} else {
			rootUri = Uri.parse(uriStr);
		}
	}

	/**
	 * Retrieves the list of file names in the root ROMs directory.
	 *
	 * @return An ArrayList of file names, or null if the root cannot be listed.
	 */
	public ArrayList<String> getRomsFileNames() {
		if (!ensureInit() || !ensureDirListed("/")) {
			return null;
		}

		ArrayList<DirEntry> dirEntries = dirFiles.get("/");
		if (dirEntries != null) {
			ArrayList<String> fileNames = new ArrayList<>();
			for (DirEntry dirEntry : dirEntries) {
				fileNames.add(dirEntry.name);
			}
			return fileNames;
		}
		return null;
	}

	/**
	 * Opens a directory for reading its entries, listing it on demand if needed.
	 *
	 * @param dirName The path of the directory to read (e.g., "/" or "/subdir/").
	 * @return A unique integer handle for the opened directory, or 0 on failure.
	 */
	public int readDir(String dirName) {
		if (!ensureInit() || !ensureDirListed(dirName)) {
			return 0;
		}

		ArrayList<DirEntry> folderFiles = dirFiles.get(dirName);
		if (folderFiles != null) {
			DirEntries entries = new DirEntries();
			entries.dirEntries = folderFiles;
			openDirs.put(entries.id, entries);
			return entries.id;
		}
		return 0;
	}

	/**
	 * Closes a previously opened directory handle.
	 *
	 * @param id The handle returned by readDir.
	 * @return 1 on success, 0 on failure.
	 */
	public int closeDir(int id) {
		if (openDirs != null && openDirs.containsKey(id)) {
			openDirs.remove(id);
			return 1;
		}
		return 0;
	}

	/**
	 * Gets the next entry from an opened directory.
	 *
	 * @param id The handle of the opened directory.
	 * @return A String array containing [name, size, modified_timestamp, type ('D' or 'F')], or null if no more entries.
	 */
	public String[] getNextDirEntry(int id) {
		if (openDirs == null) return null;

		DirEntries dirEntries = openDirs.get(id);
		if (dirEntries != null && dirEntries.dirEntIdx < dirEntries.dirEntries.size()) {
			DirEntry entry = dirEntries.dirEntries.get(dirEntries.dirEntIdx);
			dirEntries.dirEntIdx++;
			return new String[]{
				entry.name,
				String.valueOf(entry.size),
				String.valueOf(entry.modified),
				entry.isDir ? "D" : "F"
			};
		}
		return null;
	}

	/**
	 * Opens a file specified by its path and returns a detached file descriptor.
	 *
	 * @param pathName The full path of the file (e.g., "/roms/sf2.zip").
	 * @param flags    The mode to open the file with (e.g., "r", "w", "wt").
	 * @return A detached file descriptor on success, or -1 on failure.
	 */
	public int openUriFd(String pathName, String flags) {
		Log.d(TAG, "Opening URI for path: " + pathName + " with flags: " + flags);

		if (!ensureInit()) {
			return -1;
		}

		String path = "/";
		String name = pathName;
		int i = pathName.lastIndexOf("/");
		if (i != -1) {
			name = pathName.substring(i + 1);
			path = pathName.substring(0, i + 1);
		}

		String fileId = fileIDs.get(pathName);
		if (fileId == null) {
			// Not cached yet: the parent directory may not have been listed (lazy
			// mode or deeper than the scan limit). Resolve it before assuming the
			// file does not exist.
			if (ensureDirListed(path)) {
				fileId = fileIDs.get(pathName);
			}
		}

		if (fileId == null && flags.contains("w")) {
			String mimeType = "application/octet-stream";
			try {
				String parentDocId = retrieveDirId(path, flags);
				if (parentDocId != null) {
					Uri dirUri = DocumentsContract.buildDocumentUriUsingTree(rootUri, parentDocId);
					Uri docUri = DocumentsContract.createDocument(mm.getContentResolver(), dirUri, mimeType, name);
					if (docUri != null) {
						fileId = DocumentsContract.getDocumentId(docUri);
						fileIDs.put(pathName, fileId);
						DirEntry newFile = new DirEntry();
						newFile.name = name;
						newFile.docId = fileId;
						newFile.isDir = false;
						newFile.modified = System.currentTimeMillis();
						// Intentional hack: non-zero size so MAME treats the just-created
						// file (e.g. a savestate) as existing/non-empty if it re-lists the
						// directory before we re-query the real size from the provider.
						newFile.size = 1;
						ArrayList<DirEntry> parentFiles = dirFiles.get(path);
						if (parentFiles != null) {
							// Locked: the background freshness check snapshots lists.
							synchronized (parentFiles) {
								parentFiles.add(newFile);
							}
						}
						markDirty(path);
					}
				}
			} catch (Exception e) {
				Log.e(TAG, "Failed to create document: " + pathName, e);
				return -1;
			}
		}

		if (fileId != null) {
			try {
				final Uri fileUri = DocumentsContract.buildDocumentUriUsingTree(rootUri, fileId);
				if (flags.contains("w")) {
					ArrayList<DirEntry> files = dirFiles.get(path);
					if (files != null) {
						synchronized (files) {
							for (DirEntry e : files) {
								if (e.name.equals(name)) {
									e.modified = System.currentTimeMillis();
									break;
								}
							}
						}
					}
					markDirty(path);
				}
				return mm.getContentResolver().openFileDescriptor(fileUri, flags).detachFd();
			} catch (Exception e) {
				Log.e(TAG, "Failed to open file descriptor for: " + pathName, e);
				// The cache says the file exists but the provider disagrees: the
				// tree changed behind our back (file removed/renamed externally).
				notifyStaleCache();
				return -1;
			}
		}
		//Log.w(TAG, "File ID not found for path: " + pathName);
		return -1;
	}

	/**
	 * Retrieves or creates a directory's document ID, resolving existing dirs
	 * first (lazily) so createDocument never creates a duplicate like "dir (1)".
	 * @return the document ID, or null on failure.
	 */
	private String retrieveDirId(String path, String flags) {
		if (path == null || path.isEmpty()) {
			return null;
		}
		String id = fileIDs.get(path);
		if (id == null && ensureDirListed(path)) {
			id = fileIDs.get(path);
		}
		if (id != null) {
			return id;
		}
		if (!flags.contains("t")) {
			return null;
		}
		String parentPath;
		String dirName;
		String trimmedPath = path.substring(0, path.length() - 1);
		int i = trimmedPath.lastIndexOf('/');
		if (i == -1) {
			return null;
		}
		parentPath = path.substring(0, i + 1);
		dirName = trimmedPath.substring(i + 1);
		String parentId = retrieveDirId(parentPath, flags);
		if (parentId != null) {
			try {
				Uri parentDirUri = DocumentsContract.buildDocumentUriUsingTree(rootUri, parentId);
				Uri newDirUri = DocumentsContract.createDocument(mm.getContentResolver(), parentDirUri, DocumentsContract.Document.MIME_TYPE_DIR, dirName);
				if (newDirUri != null) {
					id = DocumentsContract.getDocumentId(newDirUri);
					fileIDs.put(path, id);
					// A newly created directory is empty, hence already listed.
					dirFiles.put(path, new ArrayList<>());
					DirEntry newDir = new DirEntry();
					newDir.name = dirName;
					newDir.docId = id;
					newDir.isDir = true;
					newDir.modified = System.currentTimeMillis();
					// Same hack as for files: non-zero size for freshly created dirs.
					newDir.size = 1;
					ArrayList<DirEntry> parentFiles = dirFiles.get(parentPath);
					if (parentFiles != null) {
						// Locked: the background freshness check snapshots lists.
						synchronized (parentFiles) {
							parentFiles.add(newDir);
						}
					}
					markDirty(parentPath);
					return id;
				}
			} catch (Exception e) {
				Log.e(TAG, "Failed to create subdirectory: " + dirName, e);
				return null;
			}
		}
		return null;
	}

	/**
	 * Makes sure the in-memory maps exist: only seeds the root entry (not a
	 * full blocking rescan), directories are then resolved lazily on request.
	 * @return false if no SAF URI is set.
	 */
	private boolean ensureInit() {
		if (fileIDs != null && dirFiles != null) {
			return true;
		}
		if (rootUri == null) {
			Log.e(TAG, "SAF URI is not set.");
			return false;
		}
		Log.w(TAG, "SAF access before initialization. Starting in lazy mode.");
		initMaps();
		return true;
	}

	private void initMaps() {
		Map<String, String> fi = new ConcurrentHashMap<>();
		Map<String, ArrayList<DirEntry>> df = new ConcurrentHashMap<>();
		fi.put("/", DocumentsContract.getTreeDocumentId(rootUri));
		fileIDs = fi;
		dirFiles = df;
		cacheComplete = false;
		cacheDirty = false;
		dirtyDirs.clear();
	}

	/**
	 * Marks a directory MAME wrote into (not its ancestors, unaffected). Queued
	 * for re-query and a one-shot persist on exit; a tiny journal write (only
	 * when a new dir is added) lets the next launch recover from a mid-session kill.
	 */
	private void markDirty(String dirPath) {
		cacheDirty = true;
		boolean changed = dirtyDirs.add(dirPath);
		if (changed && cacheComplete && isCachePrefEnabled()) {
			writeJournal();
		}
	}

	private boolean isCachePrefEnabled() {
		PrefsHelper prefs = mm.getPrefsHelper();
		return prefs == null || prefs.isSAFCacheEnabled();
	}

	/**
	 * Initializes SAF access without a full scan: loads the persisted cache if
	 * valid (freshness checked in the background) or starts with empty maps
	 * filled lazily on demand. @return false only if no SAF URI is set.
	 */
	public boolean initLazy() {
		if (rootUri == null) {
			Log.e(TAG, "SAF URI is not set. Cannot init SAF access.");
			return false;
		}
		staleWarned = false;

		PrefsHelper prefs = mm.getPrefsHelper();
		if (prefs != null && prefs.isSAFRescanPending()) {
			// One-shot rescan request: invalidating the persisted cache is all
			// that is needed here, the tree is simply resolved fresh (lazily).
			deleteCacheFile();
			prefs.setSAFRescanPending(false);
		}
		if (!isCachePrefEnabled()) {
			deleteCacheFile();
		}

		if (getCacheFile().exists()) {
			//Direct call. WarnWidget handles the UI thread internally.
			WarnWidget pwLoad = new WarnWidget(mm, "", mm.getString(com.seleuco.mame4droid.R.string.saf_cache_reading), Color.WHITE, false, false);
			pwLoad.init();
			boolean loaded = loadCache() && applyJournal();
			pwLoad.end();
			if (loaded) {
				cacheComplete = true;
				startBackgroundRootCheck();
				return true;
			}
		}
		Log.i(TAG, "No usable SAF cache. Starting in lazy mode.");
		initMaps();
		return true;
	}

	/**
	 * Builds the full in-memory cache, reusing the persisted cache if still
	 * fresh or falling back to a parallel full scan. Long-running: MUST be
	 * called from a background thread. @param reload forces a full rescan.
	 */
	public boolean listUriFiles(boolean reload) {
		if (fileIDs != null && !reload) {
			return true;
		}
		if (rootUri == null) {
			Log.e(TAG, "SAF URI is not set. Cannot list files.");
			return false;
		}

		staleWarned = false;

		PrefsHelper prefs = mm.getPrefsHelper();
		boolean cacheEnabled = isCachePrefEnabled();
		boolean rescanPending = prefs != null && prefs.isSAFRescanPending();
		if (!cacheEnabled || rescanPending) {
			deleteCacheFile();
		}

		//Direct call. WarnWidget handles the UI thread internally. Shown from the
		//very start so the cache load/freshness check doesn't look like a hang.
		pw = new WarnWidget(mm, mm.getString(com.seleuco.mame4droid.R.string.saf_caching_title), mm.getString(com.seleuco.mame4droid.R.string.saf_cache_reading), Color.WHITE, false, false);
		pw.init();

		boolean cacheLoaded = cacheEnabled && !reload && loadCache() && applyJournal();

		// Freshness check: compare the root's OWN mtime (single-row query)
		// instead of listing its children (often thousands of ROMs). Falls
		// back to comparing the children listing if mtime isn't usable.
		boolean fresh = false;
		ArrayList<DirEntry> freshRoot = null;
		if (cacheLoaded) {
			try {
				long m = queryDocModified(fileIDs.get("/"));
				if (m > 0 && cachedRootMtime > 0) {
					fresh = (m == cachedRootMtime);
				} else {
					freshRoot = queryChildren(fileIDs.get("/"));
					fresh = sameEntries(dirFiles.get("/"), freshRoot);
				}
			} catch (Exception e) {
				Log.w(TAG, "Root freshness check failed.", e);
			}
		}

		if (fresh) {
			pw.end();
			pw = null;
			Log.i(TAG, "Using persisted SAF cache.");
			cacheComplete = true;
			return true;
		}

		// If a cache was loaded but is stale, reconcile only the changed
		// directories (usually a handful of queries) instead of rescanning
		// everything; fall back to the full scan if that fails.
		boolean refreshed = false;
		if (cacheLoaded) {
			Log.i(TAG, "SAF cache is stale. Refreshing changed directories.");
			try {
				if (freshRoot == null) {
					freshRoot = queryChildren(fileIDs.get("/"));
				}
				refreshed = refreshTree(freshRoot);
			} catch (Exception e) {
				Log.w(TAG, "Root listing failed.", e);
			}
		}

		AtomicBoolean hadErrors = new AtomicBoolean(false);
		boolean success = true;
		if (!refreshed) {
			pw.notifyText(mm.getString(com.seleuco.mame4droid.R.string.saf_caching_wait));
			initMaps();
			success = scanAll(hadErrors);
		}

		//Direct call. WarnWidget handles the UI thread internally.
		if (pw != null) {
			pw.end();
			pw = null;
		}

		if (!success) {
			deleteCacheFile();
			showPermissionsErrorDialog();
			return false;
		}

		// Never persist a scan that failed halfway: it would freeze a broken
		// view of the tree. The in-memory maps stay usable (lazy listing can
		// still resolve the missing parts on demand).
		cacheComplete = !hadErrors.get();
		if (cacheComplete && cacheEnabled) {
			saveCache();
			if (rescanPending && prefs != null) {
				// The forced rescan has been honored; switch the one-shot off.
				prefs.setSAFRescanPending(false);
			}
		} else {
			if (!cacheComplete) {
				Log.w(TAG, "SAF scan finished with errors. Cache will not be persisted.");
			}
			deleteCacheFile();
		}
		return true;
	}

	/**
	 * Full scan using a small thread pool: SAF children queries are IPC-bound,
	 * so a few directories in flight give a large speedup on big collections.
	 *
	 * @param hadErrors Set if any non-root directory failed to list.
	 * @return false only if the root directory itself could not be listed.
	 */
	private boolean scanAll(AtomicBoolean hadErrors) {
		final ExecutorService pool = Executors.newFixedThreadPool(SCAN_THREADS);
		final AtomicInteger pending = new AtomicInteger(1);
		final CountDownLatch done = new CountDownLatch(1);
		final AtomicBoolean rootFailed = new AtomicBoolean(false);

		final String rootId = fileIDs.get("/");
		pool.execute(() -> scanDir("/", rootId, 0, pool, pending, done, hadErrors, rootFailed));

		try {
			done.await();
		} catch (InterruptedException e) {
			Thread.currentThread().interrupt();
			rootFailed.set(true);
		}
		pool.shutdown();

		return !rootFailed.get();
	}

	private void scanDir(String dirPath, String docId, int depth, ExecutorService pool,
						 AtomicInteger pending, CountDownLatch done,
						 AtomicBoolean hadErrors, AtomicBoolean rootFailed) {
		try {
			ArrayList<DirEntry> entries = queryChildren(docId);
			commitDir(dirPath, entries);
			for (DirEntry entry : entries) {
				if (entry.isDir) {
					// This is an intentional depth limit as an optimization; deeper
					// directories are resolved lazily on demand.
					if (depth + 1 < SCAN_DEPTH_LIMIT) {
						final String childPath = childPath(dirPath, entry);
						final String childId = entry.docId;
						pending.incrementAndGet();
						pool.execute(() -> scanDir(childPath, childId, depth + 1, pool, pending, done, hadErrors, rootFailed));
					}
				} else {
					// WarnWidget handles the UI thread internally.
					if (pw != null) {
						pw.notifyText("Caching: " + entry.name);
					}
				}
			}
		} catch (Exception e) {
			Log.e(TAG, "Exception while listing directory: " + dirPath, e);
			hadErrors.set(true);
			if (depth == 0) {
				rootFailed.set(true);
			}
		} finally {
			if (pending.decrementAndGet() == 0) {
				done.countDown();
			}
		}
	}

	/**
	 * Queries the children of a directory (one ContentResolver IPC).
	 *
	 * @param docId The document ID of the directory.
	 * @return The directory entries, with their document IDs.
	 */
	private ArrayList<DirEntry> queryChildren(String docId) throws Exception {
		final Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(rootUri, docId);
		//try-with-resources
		try (Cursor c = mm.getContentResolver().query(childrenUri,
			new String[]{
				DocumentsContract.Document.COLUMN_DOCUMENT_ID,
				DocumentsContract.Document.COLUMN_DISPLAY_NAME,
				DocumentsContract.Document.COLUMN_MIME_TYPE,
				DocumentsContract.Document.COLUMN_SIZE,
				DocumentsContract.Document.COLUMN_LAST_MODIFIED
			}, null, null, null)) {

			if (c == null) {
				throw new IOException("Query returned null cursor for: " + childrenUri);
			}
			ArrayList<DirEntry> entries = new ArrayList<>();
			while (c.moveToNext()) {
				DirEntry dirEntry = new DirEntry();
				dirEntry.docId = c.getString(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DOCUMENT_ID));
				dirEntry.name = c.getString(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DISPLAY_NAME));
				dirEntry.size = c.getLong(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_SIZE));
				dirEntry.modified = c.getLong(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_LAST_MODIFIED));
				final String mimeType = c.getString(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_MIME_TYPE));
				dirEntry.isDir = DocumentsContract.Document.MIME_TYPE_DIR.equals(mimeType);
				entries.add(dirEntry);
			}
			return entries;
		}
	}

	/**
	 * Publishes a listed directory into the shared maps.
	 */
	private void commitDir(String dirPath, ArrayList<DirEntry> entries) {
		for (DirEntry entry : entries) {
			fileIDs.put(childPath(dirPath, entry), entry.docId);
		}
		dirFiles.put(dirPath, entries);
	}

	private static String childPath(String dirPath, DirEntry entry) {
		// Directory keys start and end with "/" (e.g. "/sub/"), file keys don't
		// have the trailing one (e.g. "/sub/file.zip").
		return dirPath + entry.name + (entry.isDir ? "/" : "");
	}

	/**
	 * Makes sure a directory's children are cached, resolving it component by
	 * component from the root (one query per unvisited level, only once).
	 *
	 * @param dirPath The directory path, e.g. "/" or "/sub/dir/".
	 * @return true if the directory exists and is listed.
	 */
	private boolean ensureDirListed(String dirPath) {
		if (dirPath == null || dirPath.isEmpty()) {
			return false;
		}
		if (!dirPath.endsWith("/")) {
			dirPath = dirPath + "/";
		}
		if (dirFiles.containsKey(dirPath)) {
			return true;
		}

		String current = "/";
		while (true) {
			if (!dirFiles.containsKey(current)) {
				String docId = fileIDs.get(current);
				if (docId == null) {
					// Its parent is listed and doesn't contain it: it doesn't exist.
					return false;
				}
				try {
					commitDir(current, queryChildren(docId));
				} catch (Exception e) {
					Log.e(TAG, "Lazy listing failed for: " + current, e);
					return false;
				}
			}
			if (current.equals(dirPath)) {
				return true;
			}
			int next = dirPath.indexOf('/', current.length());
			if (next == -1) {
				return false;
			}
			current = dirPath.substring(0, next + 1);
		}
	}

	// --- Cache persistence ---

	private File getCacheFile() {
		return new File(mm.getFilesDir(), CACHE_FILE_NAME);
	}

	private File getJournalFile() {
		return new File(mm.getFilesDir(), JOURNAL_FILE_NAME);
	}

	/**
	 * Deletes the persisted cache file (e.g. when the SAF folder changes),
	 * along with the dirty journal and any leftover temp file.
	 */
	public void deleteCacheFile() {
		File f = getCacheFile();
		if (f.exists() && !f.delete()) {
			Log.w(TAG, "Could not delete SAF cache file.");
		}
		getJournalFile().delete();
		new File(mm.getFilesDir(), CACHE_FILE_NAME + ".tmp").delete();
		cachedRootMtime = 0;
	}

	/**
	 * Persists the current dirty-directory set. Tiny file, written only when the
	 * set grows (see markDirty); deleted whenever the cache itself is saved.
	 */
	private void writeJournal() {
		try (DataOutputStream out = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(getJournalFile())))) {
			ArrayList<String> dirs = new ArrayList<>(dirtyDirs);
			out.writeInt(dirs.size());
			for (String dir : dirs) {
				out.writeUTF(dir);
			}
			out.flush();
		} catch (Exception e) {
			Log.w(TAG, "Failed to write SAF dirty journal.", e);
		}
	}

	/**
	 * Reconciles the just-loaded cache with the directories a previous session
	 * wrote into but didn't get to persist (process killed before the exit
	 * persist: swipe-away, restartApp, OOM). Typically re-queries just "/" and
	 * "/sta/", so a savestate is never lost from the cache.
	 *
	 * @return false if reconciliation failed (caller should discard the cache).
	 */
	private boolean applyJournal() {
		File j = getJournalFile();
		if (!j.exists()) {
			return true;
		}
		try (DataInputStream in = new DataInputStream(new BufferedInputStream(new FileInputStream(j)))) {
			int n = in.readInt();
			if (n < 0 || n > 100000) {
				throw new IOException("Bad journal entry count: " + n);
			}
			ArrayList<String> dirs = new ArrayList<>(n);
			for (int i = 0; i < n; i++) {
				dirs.add(in.readUTF());
			}
			for (String dir : dirs) {
				String docId = fileIDs.get(dir);
				if (docId == null || !dirFiles.containsKey(dir)) continue;
				commitDir(dir, queryChildren(docId));
				dirtyDirs.add(dir);
			}
			cacheDirty = true;
			// Re-anchor the freshness baseline to now: on some providers a nested
			// write can nudge the root's own mtime too, and we don't want that
			// read as a false external change on the very next comparison.
			try {
				cachedRootMtime = queryDocModified(fileIDs.get("/"));
			} catch (Exception e) {
				cachedRootMtime = 0;
			}
			Log.i(TAG, "Applied SAF dirty journal: " + dirs.size() + " dirs.");
			return true;
		} catch (Exception e) {
			Log.w(TAG, "Discarding SAF cache: dirty journal could not be applied.", e);
			deleteCacheFile();
			return false;
		}
	}

	/**
	 * Persists the in-memory cache once, when the emulator exits, if MAME wrote
	 * into it (savestates, new dirs): only the actually-written directories are
	 * re-queried first (typically just "/sta/"), not the whole tree.
	 */
	public void persistCacheIfDirty() {
		if (!cacheComplete || !cacheDirty || !isCachePrefEnabled()) {
			return;
		}
		for (String dir : dirtyDirs) {
			String docId = fileIDs.get(dir);
			if (docId == null || !dirFiles.containsKey(dir)) continue;
			try {
				commitDir(dir, queryChildren(docId));
			} catch (Exception e) {
				// Can't reconcile: drop the persisted cache so the next launch
				// rebuilds instead of trusting stale metadata.
				Log.w(TAG, "Exit refresh failed for: " + dir, e);
				deleteCacheFile();
				return;
			}
		}
		saveCache();
	}

	private void saveCache() {
		if (fileIDs == null || dirFiles == null || rootUri == null) {
			return;
		}
		// The root's own mtime is the freshness baseline for the next launch.
		long rootMtime = 0;
		try {
			rootMtime = queryDocModified(fileIDs.get("/"));
		} catch (Exception e) {
			Log.w(TAG, "Could not read root mtime; next launch will compare the root listing.", e);
		}
		File tmp = new File(mm.getFilesDir(), CACHE_FILE_NAME + ".tmp");
		boolean renamed = false;
		try {
			try (DataOutputStream out = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(tmp)))) {
				out.writeInt(CACHE_MAGIC);
				out.writeInt(CACHE_VERSION);
				out.writeUTF(rootUri.toString());
				out.writeLong(rootMtime);
				ArrayList<Map.Entry<String, ArrayList<DirEntry>>> dirs = new ArrayList<>(dirFiles.entrySet());
				out.writeInt(dirs.size());
				int nFiles = 0;
				for (Map.Entry<String, ArrayList<DirEntry>> dir : dirs) {
					out.writeUTF(dir.getKey());
					String dirId = fileIDs.get(dir.getKey());
					out.writeUTF(dirId != null ? dirId : "");
					ArrayList<DirEntry> entries = dir.getValue();
					// The emulator thread may append to this list (file creation).
					synchronized (entries) {
						out.writeInt(entries.size());
						for (DirEntry entry : entries) {
							out.writeUTF(entry.name);
							out.writeUTF(entry.docId != null ? entry.docId : "");
							out.writeLong(entry.size);
							out.writeLong(entry.modified);
							out.writeBoolean(entry.isDir);
							nFiles++;
						}
					}
				}
				out.flush();
				Log.i(TAG, "Saved SAF cache: " + dirs.size() + " dirs, " + nFiles + " entries.");
			}
			renamed = tmp.renameTo(getCacheFile());
			if (renamed) {
				cachedRootMtime = rootMtime;
				cacheDirty = false;
				dirtyDirs.clear();
				getJournalFile().delete();
			} else {
				Log.e(TAG, "Failed to move SAF cache into place.");
			}
		} catch (Exception e) {
			Log.e(TAG, "Failed to save SAF cache.", e);
		} finally {
			// Guaranteed cleanup: never leave an orphan temp file behind.
			if (!renamed && tmp.exists() && !tmp.delete()) {
				Log.w(TAG, "Could not delete SAF cache temp file.");
			}
		}
	}

	private boolean loadCache() {
		File f = getCacheFile();
		if (!f.exists()) {
			return false;
		}
		try (DataInputStream in = new DataInputStream(new BufferedInputStream(new FileInputStream(f)))) {
			if (in.readInt() != CACHE_MAGIC || in.readInt() != CACHE_VERSION) {
				throw new IOException("Bad magic/version.");
			}
			if (!in.readUTF().equals(rootUri.toString())) {
				throw new IOException("Cache belongs to another tree URI.");
			}
			long rootMtime = in.readLong();
			Map<String, String> fi = new ConcurrentHashMap<>();
			Map<String, ArrayList<DirEntry>> df = new ConcurrentHashMap<>();
			fi.put("/", DocumentsContract.getTreeDocumentId(rootUri));
			int nDirs = in.readInt();
			if (nDirs < 0 || nDirs > 4000000) {
				throw new IOException("Bad dir count: " + nDirs);
			}
			for (int d = 0; d < nDirs; d++) {
				String dirPath = in.readUTF();
				String dirId = in.readUTF();
				if (!dirId.isEmpty() && !dirPath.equals("/")) {
					fi.put(dirPath, dirId);
				}
				int nEntries = in.readInt();
				if (nEntries < 0 || nEntries > 4000000) {
					throw new IOException("Bad entry count: " + nEntries);
				}
				ArrayList<DirEntry> entries = new ArrayList<>(nEntries);
				for (int e = 0; e < nEntries; e++) {
					DirEntry entry = new DirEntry();
					entry.name = in.readUTF();
					entry.docId = in.readUTF();
					entry.size = in.readLong();
					entry.modified = in.readLong();
					entry.isDir = in.readBoolean();
					entries.add(entry);
					fi.put(childPath(dirPath, entry), entry.docId);
				}
				df.put(dirPath, entries);
			}
			fileIDs = fi;
			dirFiles = df;
			cachedRootMtime = rootMtime;
			cacheDirty = false;
			dirtyDirs.clear();
			Log.i(TAG, "Loaded SAF cache: " + nDirs + " dirs, " + fi.size() + " paths.");
			return true;
		} catch (Exception e) {
			Log.w(TAG, "Discarding unreadable SAF cache.", e);
			deleteCacheFile();
			return false;
		}
	}

	/**
	 * Queries a single document's last-modified time (one cheap single-row IPC).
	 */
	private long queryDocModified(String docId) throws Exception {
		final Uri docUri = DocumentsContract.buildDocumentUriUsingTree(rootUri, docId);
		try (Cursor c = mm.getContentResolver().query(docUri,
			new String[]{DocumentsContract.Document.COLUMN_LAST_MODIFIED}, null, null, null)) {
			if (c == null || !c.moveToFirst()) {
				throw new IOException("No document row for: " + docUri);
			}
			return c.getLong(0);
		}
	}

	// --- Cache freshness ---

	private static boolean sameEntries(ArrayList<DirEntry> cached, ArrayList<DirEntry> fresh) {
		if (cached == null || fresh == null || cached.size() != fresh.size()) {
			return false;
		}
		Map<String, DirEntry> byName = new HashMap<>();
		for (DirEntry entry : cached) {
			byName.put(entry.name, entry);
		}
		for (DirEntry entry : fresh) {
			DirEntry old = byName.get(entry.name);
			if (old == null || old.isDir != entry.isDir || old.size != entry.size || old.modified != entry.modified) {
				return false;
			}
		}
		return true;
	}

	/**
	 * Incremental reconciliation of a stale cache: only descends into cached
	 * subdirectories whose metadata changed, so a few new ROMs or a savestate
	 * cost a handful of queries, not a full rescan. @return false on failure.
	 */
	private boolean refreshTree(ArrayList<DirEntry> freshRoot) {
		try {
			refreshDir("/", freshRoot, 0);
			return true;
		} catch (Exception e) {
			Log.w(TAG, "Incremental SAF refresh failed. Falling back to a full scan.", e);
			return false;
		}
	}

	private void refreshDir(String dirPath, ArrayList<DirEntry> fresh, int depth) throws Exception {
		ArrayList<DirEntry> cached = dirFiles.get(dirPath);
		Map<String, DirEntry> old = new HashMap<>();
		if (cached != null) {
			for (DirEntry entry : cached) {
				old.put(entry.name, entry);
			}
		}
		commitDir(dirPath, fresh);
		// WarnWidget handles the UI thread internally.
		if (pw != null) {
			pw.notifyText("Refreshing: " + dirPath);
		}
		for (DirEntry entry : fresh) {
			DirEntry prev = old.remove(entry.name);
			if (entry.isDir) {
				String cp = childPath(dirPath, entry);
				if (prev != null && !prev.isDir) {
					// Type changed file -> dir: drop the stale file key.
					fileIDs.remove(dirPath + entry.name);
				}
				boolean changed = prev == null || !prev.isDir || prev.modified != entry.modified;
				if (changed && dirFiles.containsKey(cp)) {
					if (depth + 1 < SCAN_DEPTH_LIMIT) {
						refreshDir(cp, queryChildren(entry.docId), depth + 1);
					} else {
						// Beyond the eager limit: drop the cached subtree and let
						// the lazy resolver rebuild it on demand. The dir itself
						// still exists: restore its ID, which removeSubtree drops.
						removeSubtree(cp);
						fileIDs.put(cp, entry.docId);
					}
				}
			} else if (prev != null && prev.isDir) {
				// Type changed dir -> file: drop the stale subtree.
				removeSubtree(dirPath + entry.name + "/");
			}
		}
		// Whatever remains in old was removed externally.
		for (DirEntry gone : old.values()) {
			if (gone.isDir) {
				removeSubtree(dirPath + gone.name + "/");
			} else {
				fileIDs.remove(dirPath + gone.name);
			}
		}
	}

	private void removeSubtree(String dirPrefix) {
		for (Iterator<String> it = fileIDs.keySet().iterator(); it.hasNext(); ) {
			if (it.next().startsWith(dirPrefix)) it.remove();
		}
		for (Iterator<String> it = dirFiles.keySet().iterator(); it.hasNext(); ) {
			if (it.next().startsWith(dirPrefix)) it.remove();
		}
	}

	/**
	 * Runs the freshness check on a low-priority background thread so a direct
	 * game launch isn't delayed; if the cache turns out stale, the user is
	 * warned to restart (the game keeps running fine on the cached view).
	 */
	private void startBackgroundRootCheck() {
		ArrayList<DirEntry> root = dirFiles.get("/");
		if (root == null) {
			return;
		}
		// Snapshot under the list's lock (and before emulation starts anyway),
		// so the checker never iterates a list the emulator thread could be
		// mutating (see the synchronized blocks in openUriFd/retrieveDirId).
		final ArrayList<DirEntry> snapshot;
		synchronized (root) {
			snapshot = new ArrayList<>(root);
		}
		final String rootId = fileIDs.get("/");
		final long expectedRootMtime = cachedRootMtime;
		Thread t = new Thread(() -> {
			try {
				android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_BACKGROUND);
			} catch (Exception e) {
			}
			try {
				// Cheap path: compare the root's own mtime (single-row query);
				// only if the provider gives no usable value, list and compare.
				boolean stale;
				long m = queryDocModified(rootId);
				if (m > 0 && expectedRootMtime > 0) {
					stale = (m != expectedRootMtime);
				} else {
					stale = !sameEntries(snapshot, queryChildren(rootId));
				}
				if (stale) {
					Log.w(TAG, "SAF cache is stale (root changed).");
					notifyStaleCache();
				}
			} catch (Exception e) {
				Log.w(TAG, "Background root freshness check failed.", e);
			}
		}, "safRootCheck-Thread");
		t.setDaemon(true);
		t.start();
	}

	/**
	 * Called when the cache and the real tree disagree while MAME is running:
	 * invalidates the persisted cache (next launch rescans) and warns the user,
	 * once per session, to restart.
	 */
	private void notifyStaleCache() {
		if (staleWarned) {
			return;
		}
		staleWarned = true;
		cacheComplete = false;
		deleteCacheFile();
		new WarnWidget.WarnWidgetHelper(mm, mm.getString(com.seleuco.mame4droid.R.string.saf_cache_stale), 6, Color.YELLOW, true);
	}

	/**
	 * Displays a standardized error dialog when SAF permissions fail.
	 */
	private void showPermissionsErrorDialog() {
		mm.runOnUiThread(() -> {
			String romsDir = (mm.getPrefsHelper() != null) ? mm.getPrefsHelper().getROMsDIR() : mm.getString(com.seleuco.mame4droid.R.string.saf_selected_folder);
			mm.getDialogHelper().setInfoMsg(
				mm.getString(com.seleuco.mame4droid.R.string.saf_no_permission, romsDir));
			mm.showDialog(DialogHelper.DIALOG_INFO);
		});
	}

	private StorageVolume findVolume(String name) {
		final StorageManager sm = (StorageManager) mm.getSystemService(Context.STORAGE_SERVICE);
		if ("primary".equalsIgnoreCase(name)) {
			return sm.getPrimaryStorageVolume();
		}
		for (final StorageVolume vol : sm.getStorageVolumes()) {
			final String uuid = vol.getUuid();
			if (uuid != null && uuid.equalsIgnoreCase(name)) {
				return vol;
			}
		}
		return null;
	}

	public String pathFromDocumentUri(Uri uri) {
		final List<String> pathSegments = uri.getPathSegments();
		if (pathSegments.size() < 2) return null;

		final String[] split = pathSegments.get(1).split(":");
		if (split.length < 1) return null;

		String tmp = null;
		if (split.length >= 2) {
			final StorageVolume vol = findVolume(split[0]);
			if (vol == null) return null;

			if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
				File directory = vol.getDirectory();
				if (directory != null) {
					tmp = directory.getAbsolutePath();
				}
			} else {
				try {
					Method getPathMethod = vol.getClass().getMethod("getPath");
					tmp = (String) getPathMethod.invoke(vol);
				} catch (Exception e) {
					Log.w(TAG, "Failed to get volume path via reflection.", e);
				}
			}
			if (tmp != null) {
				tmp += "/" + split[1];
			}
		} else {
			tmp = split[0].startsWith("/") ? split[0] : "/" + split[0];
		}
		return tmp;
	}
}
