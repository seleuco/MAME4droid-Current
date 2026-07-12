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

import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.Collections;
import java.util.Enumeration;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

import android.app.AlertDialog;
import android.app.Dialog;
import android.app.Service;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.SharedPreferences.Editor;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import android.graphics.Color;

import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.helpers.PrefsHelper;
import com.seleuco.mame4droid.widgets.WarnWidget;
import com.seleuco.mame4droid.R;


public class NetPlayHelper {

    /** Shared-preference key for the rollback mode toggle. */
    public static final String PREF_NETPLAY_ROLLBACK_MODE = "netplay_rollback_mode";

    protected Dialog netplayDlg = null;

    /* Waiting/connecting dialog: custom view = [small spinner + status
     * line] over a scrollable body, so big system fonts can never push the
     * Share/Peer IP buttons off screen. */
    protected AlertDialog progressDialog = null;
    private TextView progressText = null;

    private View buildProgressView(String status, String initialBody) {
        float d = mm.getResources().getDisplayMetrics().density;
        LinearLayout root = new LinearLayout(mm);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding((int) (20 * d), (int) (14 * d), (int) (20 * d), 0);
        LinearLayout row = new LinearLayout(mm);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(android.view.Gravity.CENTER_VERTICAL);
        ProgressBar pb = new ProgressBar(mm, null, android.R.attr.progressBarStyleSmall);
        row.addView(pb, new LinearLayout.LayoutParams((int) (20 * d), (int) (20 * d)));
        TextView title = new TextView(mm);
        title.setText(status);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        title.setPadding((int) (10 * d), 0, 0, 0);
        row.addView(title);
        root.addView(row);
        ScrollView sc = new ScrollView(mm);
        progressText = new TextView(mm);
        progressText.setText(initialBody);
        progressText.setPadding(0, (int) (12 * d), 0, (int) (8 * d));
        sc.addView(progressText);
        root.addView(sc, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
        return root;
    }

    /* First clipboard item as trimmed text, or null if empty/unavailable. */
    private String readClipboard() {
        try {
            android.content.ClipboardManager cb = (android.content.ClipboardManager)
                    mm.getSystemService(Context.CLIPBOARD_SERVICE);
            if (cb == null || !cb.hasPrimaryClip()) return null;
            android.content.ClipData clip = cb.getPrimaryClip();
            if (clip == null || clip.getItemCount() == 0) return null;
            CharSequence t = clip.getItemAt(0).coerceToText(mm);
            return t == null ? null : t.toString().trim();
        } catch (Exception e) {
            return null;
        }
    }

    private volatile boolean canceled = false;

    /* Host waiting-dialog text is composed by two racing workers (netplayInit
     * and the UPnP mapper): both funnel through postHostMessage().  While no
     * UPnP mapping exists the dialog shows the punch/forward hint instead. */
    private volatile String hostBaseMsg = null;
    private volatile String upnpLine = null;
    private volatile String upnpFallbackHint = "";

    /* Local address for the Share sheet; null when joining (the client only
     * ever shares its public tuple) or on mobile-only hosts. */
    private volatile String shareLocalAddr = null;

    /* Body of the connecting dialog on a LAN join: nothing to share or
     * exchange there, so it explains the situation instead. */
    private String lanConnectBody() {
        return mm.getString(R.string.np_lan_connect_body);
    }

    /** True when the user has selected ROLLBACK mode for the next session. */
    private boolean rollbackMode = false;

    protected MAME4droid mm = null;

    private static final Pattern IPV4_PATTERN =
            Pattern.compile(
                    "^(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}$");

    public NetPlayHelper(MAME4droid mm) {
        this.mm = mm;
    }

    /* High-performance Wi-Fi lock: Android's power-save duty-cycles the
     * radio on idle-looking traffic, and netplay's tiny UDP stream qualifies
     * -- that caused periodic RTT spikes big enough to saturate the rollback
     * window and stall both peers.  Held for the whole session; acquire/
     * release are idempotent (reference counting is off). */
    private WifiManager.WifiLock wifiLock = null;

    public void acquireWifiLock() {
        try {
            if (wifiLock == null) {
                WifiManager wifi = (WifiManager) mm.getApplicationContext()
                        .getSystemService(Context.WIFI_SERVICE);
                if (wifi == null) return;
                int mode = Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q
                        ? WifiManager.WIFI_MODE_FULL_LOW_LATENCY
                        : WifiManager.WIFI_MODE_FULL_HIGH_PERF;
                wifiLock = wifi.createWifiLock(mode, "MAME4droid:netplay");
                wifiLock.setReferenceCounted(false);
            }
            if (!wifiLock.isHeld()) {
                wifiLock.acquire();
                Log.d("MAME4droid_Netplay", "WifiLock acquired ("
                        + (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q
                           ? "FULL_LOW_LATENCY" : "FULL_HIGH_PERF") + ")");
            }
        } catch (Throwable e) {
            e.printStackTrace();
        }
    }

    public void releaseWifiLock() {
        try {
            if (wifiLock != null && wifiLock.isHeld()) {
                wifiLock.release();
                Log.d("MAME4droid_Netplay", "WifiLock released");
            }
        } catch (Throwable e) {
            e.printStackTrace();
        }
    }

    DialogInterface.OnCancelListener dialogCancelListener = new DialogInterface.OnCancelListener() {
        public void onCancel(DialogInterface dialog) {
            Emulator.resume();
        }
    };

    protected void prepareButtons() {

        final Button startButton = (Button) netplayDlg.findViewById(R.id.StartGameBtn);
        final Button joinButton = (Button) netplayDlg.findViewById(R.id.JoinPeerGameBtn);
        final Button disconnectButton = (Button) netplayDlg.findViewById(R.id.DisconnectBtn);
        final Button resyncButton = (Button) netplayDlg.findViewById(R.id.ResyncBtn);

        if (Emulator.getValue(Emulator.NETPLAY_HAS_CONNECTION) == 1) {
            startButton.setEnabled(false);
            joinButton.setEnabled(false);
            disconnectButton.setEnabled(true);
            /* resync only makes sense on a LIVE ROLLBACK session (the
             * native value also covers the big-state fallback that silently
             * switches a session to lockstep). */
            resyncButton.setEnabled(Emulator.getValue(Emulator.NETPLAY_IN_ROLLBACK) == 1);
        } else {
            startButton.setEnabled(true);
            joinButton.setEnabled(true);
            disconnectButton.setEnabled(false);
            resyncButton.setEnabled(false);
            /* if the session died natively since the last UI interaction,
             * drop the radio lock and the router mapping now.             */
            releaseWifiLock();
            deleteUpnpMappingAsync();
        }

        String name = Emulator.getValueStr(Emulator.GAME_SELECTED);
        if (name != null && name.length() != 0) {
            startButton.setText(mm.getString(R.string.np_start_game_named, name));
        } else {
            startButton.setText(mm.getString(R.string.np_start_game));
            startButton.setEnabled(false);
        }
    }

    public void createDialog() {

        if (!Emulator.isEmulating())
            return;

        netplayDlg = new Dialog(mm);

        netplayDlg.setContentView(R.layout.netplayview);
        netplayDlg.setTitle(mm.getString(R.string.np_dialog_title));
        netplayDlg.setCancelable(true);
        netplayDlg.setOnCancelListener(dialogCancelListener);

        final Button startButton = (Button) netplayDlg.findViewById(R.id.StartGameBtn);
        startButton.setOnClickListener(createGameClick);

        final Button joinButton = (Button) netplayDlg.findViewById(R.id.JoinPeerGameBtn);
        joinButton.setOnClickListener(joinGameClick);

        final Button disconnectButton = (Button) netplayDlg.findViewById(R.id.DisconnectBtn);
        disconnectButton.setOnClickListener(disconnectGameClick);

        final Button resyncButton = (Button) netplayDlg.findViewById(R.id.ResyncBtn);
        resyncButton.setOnClickListener(resyncGameClick);

        prepareButtons();

        netplayDlg.show();
    }

    protected static boolean isIPv4Address(final String input) {
        return IPV4_PATTERN.matcher(input).matches();
    }

    /* The single most useful LAN IPv4 for the dialog and the Share text
     * (wlan wins over ethernet/tethering); null on mobile-only devices. */
    private String getMainLocalIPv4() {
        String first = null;
        try {
            List<NetworkInterface> interfaces = Collections.list(NetworkInterface.getNetworkInterfaces());
            for (NetworkInterface intf : interfaces) {
                String name = intf.getName().toLowerCase();
                if (name.contains("rmnet") || name.contains("ccmni") || name.contains("p2p") || name.contains("dummy")) continue;
                for (InetAddress addr : Collections.list(intf.getInetAddresses())) {
                    if (addr.isLoopbackAddress()) continue;
                    String s = addr.getHostAddress().toUpperCase(Locale.getDefault());
                    if (!isIPv4Address(s)) continue;
                    if (name.contains("wlan")) return s;
                    if (first == null) first = s;
                }
            }
        } catch (Exception ex) {
            ex.printStackTrace();
        }
        return first;
    }

    /* Every non-loopback IPv4 except carrier interfaces, so the host can share
     * ALL its LAN/hotspot addresses and the peer keeps the one on its own /24
     * (a hotspot host often has both a mobile and a reachable AP address). */
    private java.util.List<String> getAllLocalIPv4() {
        java.util.List<String> out = new java.util.ArrayList<String>();
        try {
            for (NetworkInterface intf : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                String name = intf.getName().toLowerCase();
                if (name.contains("rmnet") || name.contains("ccmni") || name.contains("p2p") || name.contains("dummy")) continue;
                for (InetAddress addr : Collections.list(intf.getInetAddresses())) {
                    if (addr.isLoopbackAddress()) continue;
                    String s = addr.getHostAddress().toUpperCase(Locale.getDefault());
                    if (isIPv4Address(s) && !out.contains(s)) out.add(s);
                }
            }
        } catch (Exception ex) {
            ex.printStackTrace();
        }
        return out;
    }

    /* Android share sheet with the addresses ready to paste on the other
     * end -- nobody should ever transcribe an IP by hand. */
    private void shareAddresses(int port) {
        StringBuilder sb = new StringBuilder(mm.getString(R.string.np_share_header)).append('\n');
        /* Host (shareLocalAddr != null) advertises every LAN/hotspot address so
         * the peer can pick a reachable one; the client shares only its public. */
        if (shareLocalAddr != null)
            for (String loc : getAllLocalIPv4())
                sb.append(mm.getString(R.string.np_share_same_network, loc + ":" + port)).append('\n');
        String info = Emulator.netplayGetPublicAddr();
        if (info != null && info.length() > 0)
            sb.append(mm.getString(R.string.np_share_internet, info.split("\\|")[0]));
        Intent i = new Intent(Intent.ACTION_SEND);
        i.setType("text/plain");
        i.putExtra(Intent.EXTRA_TEXT, sb.toString().trim());
        try {
            mm.startActivity(Intent.createChooser(i, mm.getString(R.string.np_share_title)));
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    /* Both ip[:port] candidates in pasted text: [private, public], each null if
     * absent.  Labels are ignored (classification is by IP value), so it works
     * whatever locale the shared invite was written in. */
    private String[] addressCandidates(String s) {
        String privFirst = null, privOnSubnet = null, pub = null;
        if (s != null) {
            java.util.regex.Matcher m = java.util.regex.Pattern.compile(
                    "(\\d{1,3}(?:\\.\\d{1,3}){3})(:\\d{1,5})?").matcher(s);
            while (m.find()) {
                String ipStr = m.group(1);
                if (!isIPv4Address(ipStr)) continue;
                if (isPrivateIPv4(ipStr)) {
                    if (privFirst == null) privFirst = m.group();
                    if (privOnSubnet == null && sameSubnet24(m.group())) privOnSubnet = m.group();
                } else if (pub == null) pub = m.group();
            }
        }
        /* Prefer a private on OUR /24 (reachable) when the host shared several. */
        return new String[]{ privOnSubnet != null ? privOnSubnet : privFirst, pub };
    }

    /* Strip an optional ":port" -> bare IPv4. */
    private static String ipOnly(String ipWithPort) {
        if (ipWithPort == null) return null;
        int c = ipWithPort.indexOf(':');
        return c > 0 ? ipWithPort.substring(0, c) : ipWithPort;
    }

    /* Whether a private ip[:port] shares our own /24 (very likely same LAN). */
    private boolean sameSubnet24(String privWithPort) {
        String own = getMainLocalIPv4();
        String priv = ipOnly(privWithPort);
        if (own == null || priv == null) return false;
        return own.substring(0, own.lastIndexOf('.') + 1)
                .equals(priv.substring(0, priv.lastIndexOf('.') + 1));
    }

    /* Pulls a usable ip[:port] out of pasted text: private on our /24 wins
     * (same-network), then public, else raw.  Synchronous heuristic, also the
     * fallback when the public-IP probe can't run (see resolveAndJoin). */
    protected String extractAddress(String s) {
        if (s == null) return "";
        String[] c = addressCandidates(s);
        String priv = c[0], pub = c[1];
        if (priv != null && sameSubnet24(priv)) return priv;
        if (pub != null) return pub;
        if (priv != null) return priv;
        return s.trim();
    }

    /* Decide LAN vs internet for a pasted invite, then join.  With BOTH host
     * IPs, a STUN probe compares publics: equal = same site -> LAN, else
     * internet; probe empty (offline / blocked) -> /24 heuristic. */
    private void resolveAndJoin(final String pasted) {
        String[] c = addressCandidates(pasted);
        final String priv = c[0], pub = c[1];
        if (priv != null && pub != null) {
            final String hostPubIp = ipOnly(pub);
            new Thread(new Runnable() { public void run() {
                String myPub = Emulator.netplayProbePublicIp();
                final String chosen = (myPub != null && myPub.length() > 0)
                        ? (myPub.equals(hostPubIp) ? priv : pub)  /* same public IP -> same site -> LAN */
                        : extractAddress(pasted);                 /* probe failed -> /24 heuristic      */
                mm.runOnUiThread(new Runnable() { public void run() { joinGame(chosen); } });
            } }).start();
        } else {
            joinGame(extractAddress(pasted));
        }
    }

    /* Any non-loopback IPv4 at all (mobile data included): getMainLocalIPv4()
     * hides carrier interfaces on purpose, so a 4G-only host has no LAN
     * address yet still has internet -- it must not be treated as offline. */
    private boolean hasAnyIPv4() {
        try {
            List<NetworkInterface> interfaces = Collections.list(NetworkInterface.getNetworkInterfaces());
            for (NetworkInterface intf : interfaces) {
                List<InetAddress> addrs = Collections.list(intf.getInetAddresses());
                for (InetAddress addr : addrs) {
                    if (!addr.isLoopbackAddress()
                            && isIPv4Address(addr.getHostAddress().toUpperCase(Locale.getDefault())))
                        return true;
                }
            }
        } catch (Exception ex) {
        }
        return false;
    }

    /** Persist and apply the chosen rollback mode, then run the given action. */
    private void pickModeAndRun(final Runnable action) {
        // Read persisted mode
        SharedPreferences sp = mm.getPrefsHelper().getSharedPreferences();
        rollbackMode = sp.getBoolean(PREF_NETPLAY_ROLLBACK_MODE, false);

        new AlertDialog.Builder(mm)
            .setTitle(mm.getString(R.string.np_mode_title))
            .setSingleChoiceItems(
                new String[]{mm.getString(R.string.np_mode_lockstep), mm.getString(R.string.np_mode_rollback)},
                rollbackMode ? 1 : 0,
                new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int which) {
                        rollbackMode = (which == 1);
                    }
                }
            )
            .setPositiveButton(mm.getString(R.string.ok), new DialogInterface.OnClickListener() {
                public void onClick(DialogInterface dialog, int which) {
                    // Persist choice
                    SharedPreferences sp = mm.getPrefsHelper().getSharedPreferences();
                    sp.edit().putBoolean(PREF_NETPLAY_ROLLBACK_MODE, rollbackMode).apply();
                    // Apply mode to native layer BEFORE netplayInit()
                    Emulator.netplaySetMode(rollbackMode ? 1 : 0);
                    action.run();
                }
            })
            .setNegativeButton(mm.getString(R.string.cancel), null)
            .show();
    }

    /* Hosting goes straight to the waiting dialog: the punch target (hole
     * punching) is only ever armed later via its Peer IP button, and only
     * needed when the host isn't directly reachable (no UPnP/forward). */
    Button.OnClickListener createGameClick = new Button.OnClickListener() {
        public void onClick(View v) {
            pickModeAndRun(new Runnable() { public void run() { createGame(); } });
        }
    };

    /* Hot punch-target prompt while the host waits: feeds
     * netplaySetPunchAddr, which the network thread applies within ~500ms. */
    protected void promptHotPunchAddr(final int gamePort) {
        AlertDialog.Builder alert = new AlertDialog.Builder(mm);
        alert.setTitle(mm.getString(R.string.np_peer_ip_title));

        final EditText input = new EditText(mm);
        String punch = mm.getPrefsHelper().getSharedPreferences().getString(PrefsHelper.PREF_NETPLAY_PUNCHADDR, "");
        input.setText(punch);
        input.setSelection(input.getText().length());

        /* Custom view: hint + field + a Clear/Paste row (same as the join dialog). */
        float d = mm.getResources().getDisplayMetrics().density;
        LinearLayout box = new LinearLayout(mm);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding((int) (20 * d), (int) (8 * d), (int) (20 * d), 0);
        TextView hint = new TextView(mm);
        hint.setText(mm.getString(R.string.np_peer_ip_hint));
        hint.setPadding(0, 0, 0, (int) (8 * d));
        box.addView(hint);
        box.addView(input);
        LinearLayout btnRow = new LinearLayout(mm);
        btnRow.setOrientation(LinearLayout.HORIZONTAL);
        btnRow.setGravity(android.view.Gravity.END);
        Button clearBtn = new Button(mm);
        clearBtn.setText(mm.getString(R.string.clear));
        clearBtn.setOnClickListener(new View.OnClickListener() {
            public void onClick(View b) { input.setText(""); }
        });
        Button pasteBtn = new Button(mm);
        pasteBtn.setText(mm.getString(R.string.paste));
        pasteBtn.setOnClickListener(new View.OnClickListener() {
            public void onClick(View b) {
                String clip = readClipboard();
                if (clip != null && clip.length() > 0) {
                    input.setText(clip);
                    input.setSelection(input.getText().length());
                }
            }
        });
        btnRow.addView(clearBtn);
        btnRow.addView(pasteBtn);
        box.addView(btnRow);
        alert.setView(box);

        alert.setPositiveButton(mm.getString(R.string.ok), new DialogInterface.OnClickListener() {
            public void onClick(DialogInterface dialog, int whichButton) {
                /* Tolerates a pasted Share message (labels included). */
                String s = extractAddress(input.getText().toString());
                mm.getPrefsHelper().getSharedPreferences().edit()
                        .putString(PrefsHelper.PREF_NETPLAY_PUNCHADDR, s).commit();
                String host = null;
                int p = gamePort;
                if (s.length() > 0) {
                    String[] hp = splitHostPort(s);
                    host = hp[0];
                    if (hp[1] != null) { try { p = Integer.parseInt(hp[1]); } catch (Exception e) {} }
                }
                Emulator.netplaySetPunchAddr(host, p);
            }
        });
        alert.setNegativeButton(mm.getString(R.string.cancel), null);
        alert.show();
    }

    /* "host[:port]" -> {host, portStr|null}; strings with more than one ':'
     * fall back to the whole input as host. */
    protected static String[] splitHostPort(String s) {
        int i = s.lastIndexOf(':');
        if (i > 0 && i < s.length() - 1 && s.indexOf(':') == i)
            return new String[]{s.substring(0, i), s.substring(i + 1)};
        return new String[]{s, null};
    }

    /* Private/CGNAT/link-local ranges never need STUN; anything else
     * (public IP or hostname) flips the join flow into internet mode. */
    protected static boolean isPrivateIPv4(String ip) {
        if (!isIPv4Address(ip)) return false;
        try {
            String[] p = ip.split("\\.");
            int a = Integer.parseInt(p[0]), b = Integer.parseInt(p[1]);
            if (a == 10 || a == 127) return true;
            if (a == 192 && b == 168) return true;
            if (a == 172 && b >= 16 && b <= 31) return true;
            if (a == 169 && b == 254) return true;
            if (a == 100 && b >= 64 && b <= 127) return true;
        } catch (Exception e) {
        }
        return false;
    }

    /* UPnP SOAP calls are network I/O: never on the UI thread. */
    public void deleteUpnpMappingAsync() {
        if (!UpnpHelper.isMapped()) return;
        new Thread(new Runnable() {
            public void run() {
                UpnpHelper.deletePortMapping();
            }
        }).start();
    }

    /* Clean native exit arrives via the warn channel with the connection
     * flag already 0.  Pre-join TOASTs (e.g. a build-mismatch reject) also
     * read 0 while the host keeps waiting: the dialog check keeps those
     * from unmapping the port mid-wait. */
    public void onNetplaySessionGone() {
        if (progressDialog != null && progressDialog.isShowing()) return;
        deleteUpnpMappingAsync();
    }

    /** Repaint the host waiting dialog from hostBaseMsg + upnpLine. */
    private void postHostMessage() {
        final String base = hostBaseMsg;
        if (base == null) return;
        /* extra starts with "\n"; the added "\n" makes the blank line
         * between the IP block and the UPnP/fallback status. */
        final String extra = (upnpLine != null) ? upnpLine : upnpFallbackHint;
        final String msg = extra.isEmpty() ? base : base + "\n" + extra;
        mm.runOnUiThread(new Runnable() {
            public void run() {
                if (progressDialog != null && progressDialog.isShowing() && progressText != null)
                    progressText.setText(msg);
            }
        });
    }

    /* Public-tuple lines for the waiting/connecting dialogs (worker thread,
     * after netplayInit returned).  The "unavailable" warning only shows
     * when internet play is actually in play, so LAN sessions stay clean.
     * The full diagnostics block stays NLOG-only. */
    private String publicInfoLines(boolean stunRan, boolean warnUnavailable) {
        StringBuilder sb = new StringBuilder();
        if (stunRan) {
            String info = Emulator.netplayGetPublicAddr();
            if (info != null && info.length() > 0) {
                String[] parts = info.split("\\|");
                sb.append('\n').append(mm.getString(R.string.np_public_ip, parts[0]));
                if (info.contains("sym=1"))
                    sb.append('\n').append(mm.getString(R.string.np_symmetric_nat));
            } else if (warnUnavailable) {
                sb.append('\n').append(mm.getString(R.string.np_public_unavailable));
            }
        }
        return sb.toString();
    }

    Button.OnClickListener joinGameClick = new Button.OnClickListener() {
        public void onClick(View v) {
            AlertDialog.Builder alert = new AlertDialog.Builder(mm);

            alert.setTitle(mm.getString(R.string.np_enter_peer_ip));

            final EditText input = new EditText(mm);
            String ip = mm.getPrefsHelper().getSharedPreferences().getString(PrefsHelper.PREF_NETPLAY_PEERADDR, "");
            input.setText(ip);
            input.setSelection(input.getText().length());

            /* Custom view: hint + field + a Clear/Paste row (pasting the shared
             * invite by hand is fiddly, so give it a one-tap button). */
            float d = mm.getResources().getDisplayMetrics().density;
            LinearLayout box = new LinearLayout(mm);
            box.setOrientation(LinearLayout.VERTICAL);
            box.setPadding((int) (20 * d), (int) (8 * d), (int) (20 * d), 0);
            TextView hint = new TextView(mm);
            hint.setText(mm.getString(R.string.np_join_hint));
            hint.setPadding(0, 0, 0, (int) (8 * d));
            box.addView(hint);
            box.addView(input);
            LinearLayout btnRow = new LinearLayout(mm);
            btnRow.setOrientation(LinearLayout.HORIZONTAL);
            btnRow.setGravity(android.view.Gravity.END);
            Button clearBtn = new Button(mm);
            clearBtn.setText(mm.getString(R.string.clear));
            clearBtn.setOnClickListener(new View.OnClickListener() {
                public void onClick(View b) { input.setText(""); }
            });
            Button pasteBtn = new Button(mm);
            pasteBtn.setText(mm.getString(R.string.paste));
            pasteBtn.setOnClickListener(new View.OnClickListener() {
                public void onClick(View b) {
                    String clip = readClipboard();
                    if (clip != null && clip.length() > 0) {
                        input.setText(clip);
                        input.setSelection(input.getText().length());
                    }
                }
            });
            btnRow.addView(clearBtn);
            btnRow.addView(pasteBtn);
            box.addView(btnRow);
            alert.setView(box);

                    alert.setPositiveButton(mm.getString(R.string.ok), new DialogInterface.OnClickListener() {
                public void onClick(DialogInterface dialog, int whichButton) {
                    /* Tolerates a pasted Share message (labels, both lines). */
                    final String raw = input.getText().toString();
                    final String ip = extractAddress(raw);

                    if (ip.length() == 0) {
                        new WarnWidget.WarnWidgetHelper(mm, mm.getString(R.string.np_invalid_ip), 3, Color.RED, false);
                        return;
                    }

                    InputMethodManager imm = (InputMethodManager) mm.getSystemService(Service.INPUT_METHOD_SERVICE);
                    imm.hideSoftInputFromWindow(input.getWindowToken(), 0);

                    SharedPreferences sp = mm.getPrefsHelper().getSharedPreferences();
                    Editor edit = sp.edit();
                    edit.putString(PrefsHelper.PREF_NETPLAY_PEERADDR, ip);
                    edit.commit();

                    /* Only when BOTH addresses are present (our Share text) does
                     * resolveAndJoin probe our public IP; a single pasted IP is
                     * used as-is. */
                    resolveAndJoin(raw);
                }
            });

            alert.setNegativeButton(mm.getString(R.string.cancel), new DialogInterface.OnClickListener() {
                public void onClick(DialogInterface dialog, int whichButton) {
                    // Canceled.
                }
            });

            AlertDialog dlg = alert.create();
            dlg.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_HIDDEN);
            dlg.show();
        }
    };

    Button.OnClickListener disconnectGameClick = new Button.OnClickListener() {
        public void onClick(View v) {
            Emulator.setValue(Emulator.NETPLAY_HAS_CONNECTION, 0);
            releaseWifiLock();
            deleteUpnpMappingAsync();
            com.seleuco.mame4droid.widgets.StatsWidget.hide(mm);
            new WarnWidget.WarnWidgetHelper(mm, mm.getString(R.string.np_disconnected_game), 3, Color.YELLOW, false);
            prepareButtons();
        }
    };

    /* Mid-game state resync (rollback only): latches the episode natively
     * (host recaptures + streams its state; client adopts it) and closes
     * the dialog so emulation resumes straight into the sync freeze. */
    Button.OnClickListener resyncGameClick = new Button.OnClickListener() {
        public void onClick(View v) {
            if (Emulator.netplayResync() == 1) {
                netplayDlg.dismiss();
                Emulator.resume();
            } else {
                new WarnWidget.WarnWidgetHelper(mm, mm.getString(R.string.np_resync_unavailable), 3, Color.YELLOW, false);
                prepareButtons();
            }
        }
    };

    public void createGame() {

        String strPort = mm.getPrefsHelper().getNetplayPort();
        int port = 0;
        try {
            port = Integer.parseInt(strPort);
        } catch (Exception e) {
        }
        if (!(port >= 1024 && port <= 32768 * 2)) {
            new WarnWidget.WarnWidgetHelper(mm, mm.getString(R.string.np_invalid_port), 3, Color.RED, false);
            return;
        }
        final int gamePort = port;

        Emulator.netplaySetDesyncDetectorEnabled(mm.getPrefsHelper().isNetplayDesyncDetectorEnabled() ? 1 : 0);

        /* Apply BEFORE netplayInit(): netplay_init_handle() preserves
         * frame_skip exactly as it finds it, defaulting to 2 (=1 UI frame)
         * otherwise -- setting it later (once has_joined) mostly no-ops,
         * see netplay_ui_set_delay's "not yet joined" branch. */
        Emulator.setValue(Emulator.NETPLAY_DELAY, mm.getPrefsHelper().getNetplayDelayValue());

        canceled = false;
        AlertDialog.Builder waitBld = new AlertDialog.Builder(mm);
        waitBld.setTitle(mm.getString(R.string.np_press_back_cancel));
        waitBld.setView(buildProgressView(mm.getString(R.string.np_waiting_peer), mm.getString(R.string.np_getting_info)));
        waitBld.setCancelable(true);
        waitBld.setOnCancelListener(new DialogInterface.OnCancelListener() {
            @Override
            public void onCancel(DialogInterface dialog) {
                canceled = true;
            }
        });
        /* Null listeners at build time, real ones AFTER show(): stock alert
         * buttons auto-dismiss otherwise.  Share = system share sheet with
         * the addresses; Peer IP = hot punch-target entry (each side only
         * learns its own public tuple once its flow starts). */
        waitBld.setPositiveButton(mm.getString(R.string.np_btn_share), (DialogInterface.OnClickListener) null);
        waitBld.setNeutralButton(mm.getString(R.string.np_btn_peer_ip), (DialogInterface.OnClickListener) null);
        progressDialog = waitBld.create();
        progressDialog.show();
        final Button peerBtn = progressDialog.getButton(DialogInterface.BUTTON_NEUTRAL);
        if (peerBtn != null) {
            /* Off until init is done: a punch target set before the worker's
             * netplaySetPunchAddr(null,0) clear would be wiped, and internet
             * viability isn't known until STUN.  Re-enabled (or hidden) below. */
            peerBtn.setEnabled(false);
            peerBtn.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) {
                    promptHotPunchAddr(gamePort);
                }
            });
        }
        final Button shareBtn = progressDialog.getButton(DialogInterface.BUTTON_POSITIVE);
        if (shareBtn != null) {
            /* Off until the worker has BOTH addresses (local IP + STUN): an
             * early tap would share a half-empty or stale (previous-session)
             * message.  Re-enabled once postHostMessage() runs below. */
            shareBtn.setEnabled(false);
            shareBtn.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) {
                    shareAddresses(gamePort);
                }
            });
        }
        upnpFallbackHint = "\n" + mm.getString(R.string.np_upnp_fallback_hint, gamePort);

        Thread t = new Thread(new Runnable() {
            public void run() {
                hostBaseMsg = null;
                upnpLine = null;
                if (mm.getPrefsHelper().isNetplayUpnpEnabled()) {
                    /* Runs in parallel with init/STUN: asks the router to
                     * forward the game port (automates the port-forward
                     * fallback, rescues symmetric-NAT/CGNAT peers). */
                    new Thread(new Runnable() {
                        public void run() {
                            if (UpnpHelper.addPortMapping(gamePort)) {
                                if (canceled) {
                                    UpnpHelper.deletePortMapping();
                                    return;
                                }
                                upnpLine = "\n" + mm.getString(R.string.np_upnp_mapped);
                                postHostMessage();
                            }
                        }
                    }).start();
                }

                final String ip = getMainLocalIPv4();
                shareLocalAddr = ip;
                if (ip == null && !hasAnyIPv4()) {
                    try {
                        Thread.sleep(2000);
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                    canceled = true;
                    mm.runOnUiThread(new Runnable() {
                        public void run() {
                            new WarnWidget.WarnWidgetHelper(mm, mm.getString(R.string.np_no_network), 4, Color.RED, false);
                        }
                    });
                }

                if (!canceled) {
                    /* Native init (socket+bind+STUN) blocks up to ~3s: it
                     * must run on this worker, never on the UI thread.
                     * The host ALWAYS runs STUN (its public address is the
                     * first thing to share); the punch target starts clear
                     * and is armed via the Peer IP button if ever needed. */
                    Emulator.netplaySetPunchAddr(null, 0);
                    Emulator.netplaySetLocalPort(gamePort);
                    Emulator.netplaySetInternetMode(1);

                    if (Emulator.netplayInit(null, gamePort, 0) == -1) {
                        canceled = true;
                        mm.runOnUiThread(new Runnable() {
                            public void run() {
                                new WarnWidget.WarnWidgetHelper(mm, mm.getString(R.string.np_error_init), 3, Color.RED, false);
                            }
                        });
                    } else {
                        acquireWifiLock(); /* Radio at full power for the session */
                    }
                }

                if (!canceled) {
                    if (Emulator.netplayGetPublicAddr().length() == 0) {
                        /* No public IP -> internet play isn't on the table:
                         * the Peer IP button and its hint would only mislead. */
                        upnpFallbackHint = "";
                        mm.runOnUiThread(new Runnable() {
                            public void run() {
                                if (peerBtn != null) peerBtn.setVisibility(View.GONE);
                            }
                        });
                    }

                    hostBaseMsg = (ip != null
                                    ? mm.getString(R.string.np_local_ip, ip)
                                    : mm.getString(R.string.np_mobile_only))
                            + publicInfoLines(true, ip == null)
                            + "\n" + mm.getString(R.string.np_tap_share);
                    postHostMessage();

                    /* Data ready: enable the buttons (kept off until here so an
                     * early tap can't share a stale message or set a punch
                     * target that init's clear would wipe).  peerBtn may already
                     * be hidden above when there is no public IP. */
                    mm.runOnUiThread(new Runnable() {
                        public void run() {
                            if (shareBtn != null) shareBtn.setEnabled(true);
                            if (peerBtn != null) peerBtn.setEnabled(true);
                        }
                    });
                }

                while (Emulator.getValue(Emulator.NETPLAY_HAS_JOINED) == 0 && !canceled) {
                    try {
                        Thread.sleep(1000);
                        //System.out.println("Esperando...");
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }

                if (progressDialog != null && progressDialog.isShowing()) {
                    progressDialog.dismiss();
                }

                if (canceled) {
                    Emulator.setValue(Emulator.NETPLAY_HAS_CONNECTION, 0);
                    releaseWifiLock();
                    UpnpHelper.deletePortMapping(); /* worker thread: sync ok */
                }

                mm.runOnUiThread(new Runnable() {
                    public void run() {
                        if (!canceled) {
                            if (netplayDlg.isShowing())
                                netplayDlg.hide();
                            new WarnWidget.WarnWidgetHelper(mm, mm.getString(R.string.np_connected), 3, Color.GREEN, false);
                            Emulator.resume();
                        }
                    }
                });
            }
        });
        t.start();
    }

    public void joinGame(String addr) {

       String strPort = mm.getPrefsHelper().getNetplayPort();
        int port = 0;
        try {
            port = Integer.parseInt(strPort);
        } catch (Exception e) {
        }
        if (!(port >= 1024 && port <= 32768 * 2)) {
            new WarnWidget.WarnWidgetHelper(mm, mm.getString(R.string.np_invalid_port), 3, Color.RED, false);
            return;
        }

        /* "ip[:port]": an explicit port overrides the pref as DESTINATION
         * (the host's NAT may have rewritten it into the shared tuple). */
        String[] hp = splitHostPort(addr.trim());
        final String destHost = hp[0];
        final int localPort = port; /* OUR bind port stays the settings one */
        int dp = port;
        if (hp[1] != null) { try { dp = Integer.parseInt(hp[1]); } catch (Exception e) {} }
        final int destPort = dp;
        final boolean inetMode = !isPrivateIPv4(destHost);
        final String addrShown = addr;

        Emulator.netplaySetDesyncDetectorEnabled(mm.getPrefsHelper().isNetplayDesyncDetectorEnabled() ? 1 : 0);

        /* Apply BEFORE netplayInit() -- see the matching comment in
         * createGame(). Note the Client's value only matters until
         * JOIN_ACK: the Host is authoritative and overwrites it then. */
        Emulator.setValue(Emulator.NETPLAY_DELAY, mm.getPrefsHelper().getNetplayDelayValue());

        canceled = false;
        shareLocalAddr = null; /* the client only shares its public tuple */
        AlertDialog.Builder joinBld = new AlertDialog.Builder(mm);
        joinBld.setTitle(mm.getString(R.string.np_press_back_cancel));
        joinBld.setView(buildProgressView(mm.getString(R.string.np_connecting_to, addr),
                inetMode ? "" : lanConnectBody()));
        joinBld.setCancelable(true);
        joinBld.setOnCancelListener(new DialogInterface.OnCancelListener() {
            @Override
            public void onCancel(DialogInterface dialog) {
                canceled = true;
            }
        });
        if (inetMode) /* a LAN join has nothing to share */
            joinBld.setPositiveButton(mm.getString(R.string.np_btn_share), (DialogInterface.OnClickListener) null);
        progressDialog = joinBld.create();
        progressDialog.show();
        Button shareBtn = progressDialog.getButton(DialogInterface.BUTTON_POSITIVE);
        if (shareBtn != null) {
            shareBtn.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) {
                    shareAddresses(destPort);
                }
            });
        }

        Thread t = new Thread(new Runnable() {
            public void run() {
                /* Native init (socket+bind+STUN) blocks up to ~3s: worker
                 * only.  The punch target is host-side config; clear any
                 * stale one from a previous hosted session. */
                Emulator.netplaySetPunchAddr(null, 0);
                Emulator.netplaySetLocalPort(localPort);
                Emulator.netplaySetInternetMode(inetMode ? 1 : 0);

                if (Emulator.netplayInit(destHost, destPort, 0) == -1) {
                    canceled = true;
                    mm.runOnUiThread(new Runnable() {
                        public void run() {
                            new WarnWidget.WarnWidgetHelper(mm, mm.getString(R.string.np_error_init), 3, Color.RED, false);
                        }
                    });
                } else {
                    acquireWifiLock(); /* Radio at full power for the session */
                    String pub = publicInfoLines(inetMode, inetMode);
                    if (pub.startsWith("\n")) pub = pub.substring(1);
                    /* Own public IP == join target: both devices sit behind
                     * the same router (at home the two tuples even match
                     * char by char) -- the LAN address is the way then. */
                    String sameNet = "";
                    String info = Emulator.netplayGetPublicAddr();
                    if (info != null && info.length() > 0) {
                        String myPubIp = info.split("\\|")[0];
                        int c = myPubIp.indexOf(':');
                        if (c > 0) myPubIp = myPubIp.substring(0, c);
                        if (myPubIp.equals(destHost))
                            sameNet = "\n" + mm.getString(R.string.np_same_public_ip);
                    }
                    final String msg = inetMode
                            ? pub + sameNet
                              + "\n\n" + mm.getString(R.string.np_share_hint_client)
                            : lanConnectBody();
                    mm.runOnUiThread(new Runnable() {
                        public void run() {
                            if (progressText != null)
                                progressText.setText(msg);
                        }
                    });
                }

                while (Emulator.getValue(Emulator.NETPLAY_HAS_JOINED) == 0
                        && !canceled) {
                    try {
                        if (Emulator.netplayInit(null, 0, 1) == -1)
                            canceled = true;
                        Thread.sleep(1000);
                        //System.out.println("Esperando...");
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }

                if (progressDialog != null && progressDialog.isShowing()) {
                    progressDialog.dismiss();
                }

                if (canceled) {
                    Emulator.setValue(Emulator.NETPLAY_HAS_CONNECTION, 0);
                    releaseWifiLock();
                }

                mm.runOnUiThread(new Runnable() {
                    public void run() {
                        if (!canceled) {
                            if (netplayDlg.isShowing())
                                netplayDlg.hide();
                            new WarnWidget.WarnWidgetHelper(mm, mm.getString(R.string.np_connected), 3, Color.GREEN, false);
                            Emulator.resume();
                        }
                    }
                });
            }
        });
        t.start();
    }

}
