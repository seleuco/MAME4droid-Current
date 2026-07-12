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

import java.io.File;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

// Inner class to represent a file or directory entry.
class DirEntry {
	String name;
	long size;
	long modified;
	boolean isDir;
}

// Inner class to manage the state of an opened directory for iteration.
class DirEntries {
	private static int lastId = 1;
	final int id;
	int dirEntIdx = 0; // Current index for reading entries.
	ArrayList<DirEntry> dirEntries = null;

	DirEntries() {
		this.id = lastId++;
	}
}

/**
 * A helper class to abstract interactions with Android's Storage Access Framework (SAF).
 *
 * This class provides a file-system-like interface over a URI-based directory tree.
 * It works by recursively scanning the directory tree (up to a defined depth limit)
 * and caching the metadata in memory. This allows for faster operations mimicking
 * traditional file access.
 *
 * NOTE: All expensive operations, especially listUriFiles, should be run on a background thread.
 */
public class SAFHelper {

	private static final String TAG = "SAFHelper";

	private static Uri rootUri = null;
	private static Map<String, String> fileIDs = null;
	private static Map<String, ArrayList<DirEntry>> dirFiles = null;

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
	 * @return An ArrayList of file names, or null if the cache is not built.
	 */
	public ArrayList<String> getRomsFileNames() {
		if (dirFiles == null) {
			Log.w(TAG, "getRomsFileNames called before cache was built. Forcing a reload.");
			listUriFiles(true);
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
	 * Opens a directory for reading its entries.
	 *
	 * @param dirName The path of the directory to read (e.g., "/" or "/subdir/").
	 * @return A unique integer handle for the opened directory, or 0 on failure.
	 */
	public int readDir(String dirName) {
		if (dirFiles == null) {
			Log.w(TAG, "readDir called before cache was built. Forcing a reload.");
			listUriFiles(true);
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

		if (fileIDs == null) {
			Log.w(TAG, "openUriFd called before cache was built. Forcing a reload.");
			if (!listUriFiles(true)) {
				return -1;
			}
		}

		String fileId = fileIDs.get(pathName);
		String path = "/";
		String name = pathName;
		int i = pathName.lastIndexOf("/");
		if (i != -1) {
			name = pathName.substring(i + 1);
			path = pathName.substring(0, i + 1);
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
						newFile.isDir = false;
						newFile.modified = System.currentTimeMillis();
						newFile.size = 1;
						if (dirFiles.get(path) != null) {
							dirFiles.get(path).add(newFile);
						}
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
						for (DirEntry e : files) {
							if (e.name.equals(name)) {
								e.modified = System.currentTimeMillis();
								break;
							}
						}
					}
				}
				return mm.getContentResolver().openFileDescriptor(fileUri, flags).detachFd();
			} catch (Exception e) {
				Log.e(TAG, "Failed to open file descriptor for: " + pathName, e);
				return -1;
			}
		}
		//Log.w(TAG, "File ID not found for path: " + pathName);
		return -1;
	}

	/**
	 * Recursively retrieves or creates the document ID for a given directory path.
	 *
	 * @param path  The directory path (e.g., "/newfolder/").
	 * @param flags Flags from the open call, used to check if creation is allowed.
	 * @return The document ID of the directory, or null on failure.
	 */
	private String retrieveDirId(String path, String flags) {
		if (path == null || path.isEmpty()) {
			return null;
		}
		String id = fileIDs.get(path);
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
					ArrayList<DirEntry> newFolderFiles = new ArrayList<>();
					dirFiles.put(path, newFolderFiles);
					DirEntry newDir = new DirEntry();
					newDir.name = dirName;
					newDir.isDir = true;
					newDir.modified = System.currentTimeMillis();
					newDir.size = 1;
					if (dirFiles.get(parentPath) != null) {
						dirFiles.get(parentPath).add(newDir);
					}
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
	 * Scans all files and directories from the root URI and builds the in-memory cache.
	 * This is a long-running operation and MUST be called from a background thread.
	 *
	 * @param reload If true, forces a full rescan even if the cache already exists.
	 * @return true on success, false on failure.
	 */
	public boolean listUriFiles(boolean reload) {
		if (fileIDs != null && !reload) {
			return true;
		}
		if (rootUri == null) {
			Log.e(TAG, "SAF URI is not set. Cannot list files.");
			return false;
		}

		//Direct call. WarnWidget handles the UI thread internally.
		pw = new WarnWidget(mm, mm.getString(com.seleuco.mame4droid.R.string.saf_caching_title), mm.getString(com.seleuco.mame4droid.R.string.saf_caching_wait), Color.WHITE, false, false);
		pw.init();

		fileIDs = new HashMap<>();
		dirFiles = new HashMap<>();
		String id = DocumentsContract.getTreeDocumentId(rootUri);
		fileIDs.put("/", id);
		ArrayList<DirEntry> rootEntries = new ArrayList<>();
		dirFiles.put("/", rootEntries);

		boolean success = listUriFilesRecursive(rootEntries, rootUri, "", 0);

		//Direct call. WarnWidget handles the UI thread internally.
		if (pw != null) {
			pw.end();
		}

		if (!success) {
			showPermissionsErrorDialog();
		}
		return success;
	}

	/**
	 * Recursively scans a directory URI to populate file caches, respecting a depth limit.
	 *
	 * @param folderFiles The list of entries for the current directory being scanned.
	 * @param _uri        The URI of the current directory.
	 * @param path        The relative string path of the current directory.
	 * @param depth       The current recursion depth.
	 * @return true if scanning was successful, false otherwise.
	 */
	private boolean listUriFilesRecursive(ArrayList<DirEntry> folderFiles, Uri _uri, String path, int depth) {
		// This is an intentional depth limit as an optimization.
		if (depth == 6) {
			Log.d(TAG, "Reached recursion depth limit at path: " + path);
			return true;
		}

		final String documentId = (depth == 0)
			? DocumentsContract.getTreeDocumentId(_uri)
			: DocumentsContract.getDocumentId(_uri);

		final Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(_uri, documentId);

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
				Log.w(TAG, "Query returned null cursor for: " + childrenUri);
				return depth > 0;
			}
			while (c.moveToNext()) {
				final String docId = c.getString(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DOCUMENT_ID));
				final String name = c.getString(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DISPLAY_NAME));
				final long size = c.getLong(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_SIZE));
				final long modified = c.getLong(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_LAST_MODIFIED));
				final String mimeType = c.getString(c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_MIME_TYPE));
				final String filePath = path + "/" + name;
				//final String filePath = (path + "/" + name).replaceAll("/{2,}", "/");
				final Uri docUri = DocumentsContract.buildDocumentUriUsingTree(_uri, docId);
				DirEntry dirEntry = new DirEntry();
				dirEntry.name = name;
				dirEntry.modified = modified;
				dirEntry.size = size;
				dirEntry.isDir = DocumentsContract.Document.MIME_TYPE_DIR.equals(mimeType);
				folderFiles.add(dirEntry);

				if (dirEntry.isDir) {
					String dirPath = filePath + "/";
					ArrayList<DirEntry> newFolderFiles = new ArrayList<>();
					fileIDs.put(dirPath, docId);
					dirFiles.put(dirPath, newFolderFiles);
					listUriFilesRecursive(newFolderFiles, docUri, filePath, depth + 1);
				} else {
					fileIDs.put(filePath, docId);
					// WarnWidget handles the UI thread internally.
					if (pw != null) {
						pw.notifyText("Caching: " + name);
					}
				}
			}
			return true;
		} catch (Exception e) {
			Log.e(TAG, "Exception during recursive file listing at path: " + path, e);
			return depth > 0;
		}
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
