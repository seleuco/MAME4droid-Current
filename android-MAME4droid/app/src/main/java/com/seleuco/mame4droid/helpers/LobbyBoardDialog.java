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

import android.app.AlertDialog;
import android.content.DialogInterface;
import android.graphics.Color;
import android.util.TypedValue;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.R;

import java.util.ArrayList;
import java.util.List;

/**
 * The public board: games other people are hosting right now, and one tap to
 * join one. It only fills in the addresses the user would otherwise copy by
 * hand. Refreshing follows the server cadence and backs off while nothing
 * changes: a board left open could push a small free server past its budget.
 */
public class LobbyBoardDialog {

    private static final int GOOD = 2;
    private static final int MAYBE = 1;
    private static final int HOPELESS = 0;

    private final MAME4droid mm;
    private final NetPlayHelper netplay;
    private final String base;
    private final int proto;

    private AlertDialog dialog;
    private LinearLayout listBox;
    private TextView statusText;

    private volatile boolean running;
    private volatile String etag;
    private volatile int idleRounds;
    private volatile String mySite;
    /* Our own country, kept from the sort so the rows can use it too. */
    private volatile String myCountry;
    private volatile boolean configured = false;

    /* What an empty board says instead of just "nobody is here". Rides on the
     * config call, made once when the board opens, so it costs no request of
     * its own. Null whenever the server had nothing worth showing. */
    private volatile LobbyClient.Stats showcase;

    /* Parked while the app is in the background: the loop stays alive but
     * stops calling out, so a board nobody is looking at spends neither the
     * server's traffic budget nor the battery. */
    private volatile boolean paused = false;

    /* The loop has put a definite explanation on screen (over quota, board
     * switched off). Kept apart from the cold-start
     * wait so the ticker does not overwrite it with "waking up". */
    private volatile boolean explained = false;
    private volatile long waitingSince = 0;

    /* Woken by the Refresh button so a tap acts now instead of after however
     * long the backoff was going to sleep for. */
    private final Object waiter = new Object();

    /* The listing is deliberately static so its ETag holds, which means an
     * unchanged board is answered with a 304 and never repainted. The clock
     * therefore has to run here: server time when the rows were built, plus
     * however long this device has been awake since. */
    private final List<Row> visible = new ArrayList<Row>();
    private android.os.Handler ticker;
    private long clockSeconds;
    private long clockTakenAt;

    private static class Row {
        LobbyClient.Room room;
        TextView detail;
    }


    /* The server dictates the cadence; these are only what we start from. */
    private volatile int listSeconds = 5;
    private volatile double backoff = 1.5;
    private volatile int maxSeconds = 20;

    public LobbyBoardDialog(MAME4droid mm, NetPlayHelper netplay) {
        this.mm = mm;
        this.netplay = netplay;
        this.base = mm.getPrefsHelper().getNetplayLobbyUrl();
        this.proto = Emulator.netplayGetProtocolVersion();
    }

    public void show() {
        float density = mm.getResources().getDisplayMetrics().density;

        LinearLayout root = new LinearLayout(mm);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding((int) (16 * density), (int) (8 * density), (int) (16 * density), 0);

        statusText = new TextView(mm);
        statusText.setText(mm.getString(R.string.np_lobby_loading));
        statusText.setPadding(0, 0, 0, (int) (8 * density));
        root.addView(statusText);

        listBox = new LinearLayout(mm);
        listBox.setOrientation(LinearLayout.VERTICAL);

        ScrollView scroll = new ScrollView(mm);
        scroll.addView(listBox);
        /* A floor and not a fixed height: in landscape the dialog is shorter
         * than this, and a scroll view taller than the window it sits in gets
         * clipped with nothing left for it to scroll. */
        scroll.setMinimumHeight((int) (320 * density));
        root.addView(scroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        AlertDialog.Builder builder = new AlertDialog.Builder(mm);
        builder.setTitle(mm.getString(R.string.np_lobby_title));
        builder.setView(root);
        builder.setNegativeButton(mm.getString(R.string.dismiss), null);
        builder.setNeutralButton(mm.getString(R.string.np_lobby_refresh), null);
        builder.setOnDismissListener(new DialogInterface.OnDismissListener() {
            public void onDismiss(DialogInterface d) {
                running = false;
                wake();
            }
        });

        dialog = builder.create();
        dialog.show();
        /* Gamepad default: Refresh is harmless; Dismiss would close the board. */
        NetPlayHelper.focusButton(dialog, DialogInterface.BUTTON_NEUTRAL);

        /* Refresh without dismissing: the stock button would close the board
         * on every tap. Dropping the ETag asks for the whole list again, so a
         * tap always repaints something instead of quietly getting a 304. */
        dialog.getButton(DialogInterface.BUTTON_NEUTRAL).setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                etag = null;
                idleRounds = 0;
                wake();
            }
        });

        running = true;
        startTicker();
        startRefreshLoop();
    }

    /** Repaints the waiting times once a second, without asking the server. */
    private void startTicker() {
        ticker = new android.os.Handler();
        ticker.postDelayed(new Runnable() {
            public void run() {
                if (!running) return;

                /* Says what is going on while there is still no server to
                 * talk to. Here, not in the loop, because a call made while
                 * the instance boots can sit waiting for tens of seconds --
                 * and the wording has to move on anyway. */
                if (!configured && !explained && statusText != null) {
                    long waited = android.os.SystemClock.elapsedRealtime() - waitingSince;
                    if (waited > 3000L)
                        statusText.setText(mm.getString(waited < 60000L
                                ? R.string.np_lobby_waking : R.string.np_lobby_offline));
                } else {
                    for (Row row : visible)
                        row.detail.setText(detailOf(row.room));
                }
                ticker.postDelayed(this, 1000);
            }
        }, 1000);
    }

    private void wake() {
        synchronized (waiter) {
            waiter.notifyAll();
        }
    }

    /**
     * One loop that never gives up while the board is open. A sleeping server
     * answers the first caller with a 503 from the host without ever reaching
     * our code; taking that for "switched off" ended the thread and left
     * Refresh inert. Wait and show a spinner instead.
     */
    private void startRefreshLoop() {
        new Thread(new Runnable() {
            public void run() {
                waitingSince = android.os.SystemClock.elapsedRealtime();

                while (running) {
                    if (paused) {
                        awaitResume();
                        continue;
                    }

                    if (!configured) {
                        LobbyClient.Config config = LobbyClient.config(base, proto,
                                LobbySession.appVersion(mm));

                        /* Only our own server can declare itself off, and it
                         * says so in JSON. Anything else is a bad moment. */
                        if (config.answered && !config.enabled) {
                            postStatus((config.notice != null && config.notice.length() > 0)
                                    ? config.notice : mm.getString(R.string.np_lobby_disabled));
                            return;
                        }

                        /* Switched off, not asleep: a stopped server answers
                         * 403 in well under a second, where a cold start hangs
                         * or answers 503. Keep asking slowly -- a stop gets
                         * undone, and a spent allowance comes back tomorrow. */
                        if (config.siteOff()) {
                            explained = true;
                            postStatus(mm.getString(R.string.np_lobby_disabled));
                            sleep(30000L);
                            continue;
                        }

                        /* A 429 is our own server answering, not a sleeping
                         * one, and the budget is per public address: a second
                         * player in the same house spends the same one. Calling
                         * that "waking up" and retrying every 2 s only dug the
                         * hole deeper -- the loop could never climb out. */
                        if (config.throttled()) {
                            explained = true;
                            postStatus(mm.getString(R.string.np_lobby_busy));
                            sleepThrottled(config.backoffMs());
                            continue;
                        }
                        explained = false;
                        if (!config.answered) {
                            /* Steady 2 s, not a growing backoff: waking is a
                             * one-off of 10-40 s with one person watching, and
                             * the failed call never reaches us. Past a minute
                             * it is something else, so ease off. */
                            long waited = android.os.SystemClock.elapsedRealtime() - waitingSince;
                            sleep(waited < 60000L ? 2000L : 15000L);
                            continue;
                        }

                        listSeconds = config.listSeconds;
                        backoff = config.listBackoff;
                        maxSeconds = config.listMaxSeconds;
                        showcase = config.stats;
                        configured = true;
                        waitingSince = android.os.SystemClock.elapsedRealtime();
                    }

                    LobbyClient.Board board = LobbyClient.list(base, proto, etag);

                    if (board.ok() || board.notModified) {
                        etag = board.etag;
                        explained = false;
                        if (board.notModified) {
                            idleRounds++;
                        } else {
                            idleRounds = 0;
                            render(board);
                        }
                    } else if (board.siteOff()) {
                        /* Switched off under us with the board open. Say so
                         * rather than dropping back to the config loop, which
                         * would spend a minute claiming it is waking up. */
                        explained = true;
                        postStatus(mm.getString(R.string.np_lobby_disabled));
                        sleep(30000L);
                        continue;
                    } else if (board.throttled()) {
                        /* Same budget, same explanation. Crucially not a reset
                         * of `configured`: that would send us back round the
                         * config loop and spend the one call we cannot spare. */
                        explained = true;
                        postStatus(mm.getString(R.string.np_lobby_busy));
                        sleepThrottled(board.backoffMs());
                        continue;
                    } else if (board.offline() || board.disabled()) {
                        /* Lost it again: back to asking who we are talking to,
                         * instead of hammering a listing that cannot answer. */
                        postStatus(mm.getString(R.string.np_lobby_waking));
                        configured = false;
                        continue;
                    }

                    sleep(nextDelayMs());
                }
            }
        }).start();
    }

    /**
     * Slow down while the board is not changing. Each unchanged answer costs
     * only headers, but a hundred screens left open still add up, so an idle
     * viewer drifts towards the server's ceiling and any tap on Refresh brings
     * it straight back.
     */
    private long nextDelayMs() {
        double seconds = listSeconds;
        for (int i = 0; i < idleRounds; i++) {
            seconds *= backoff;
            if (seconds >= maxSeconds) {
                seconds = maxSeconds;
                break;
            }
        }
        return (long) (seconds * 1000L);
    }

    private void render(final LobbyClient.Board board) {
        if (board.mySite != null && board.mySite.length() > 0) mySite = board.mySite;

        final List<LobbyClient.Room> fresh = board.rooms;
        sortByPromise(fresh);

        /* The NetPlay dialog underneath shows this count on its button, and
         * this is the freshest it will ever be. Costs nothing: we already
         * asked. */
        netplay.noteRoomsOnBoard(fresh.size());

        /* Trust the server's clock over the device's, which may be minutes
         * out; elapsedRealtime is what advances it from here. */
        if (board.serverTimeSeconds > 0) {
            clockSeconds = board.serverTimeSeconds;
            clockTakenAt = android.os.SystemClock.elapsedRealtime();
        }

        mm.runOnUiThread(new Runnable() {
            public void run() {
                if (!running || listBox == null) return;
                listBox.removeAllViews();
                visible.clear();

                statusText.setText(fresh.isEmpty()
                        ? mm.getString(R.string.np_lobby_empty)
                        : mm.getString(R.string.np_lobby_count, board.total));

                /* Inside listBox on purpose: every render clears it, so the
                 * showcase cannot survive into a board that has since filled
                 * up, and it comes back by itself if the last room leaves. */
                if (fresh.isEmpty()) {
                    View pitch = buildShowcase();
                    if (pitch != null) listBox.addView(pitch);
                }

                for (final LobbyClient.Room room : fresh)
                    listBox.addView(buildRow(room));
            }
        });
    }

    /**
     * Best first, because the first row is the one that gets tapped. A game on
     * our own network beats everything (no NAT to defeat), then one we can
     * actually run, then how likely the punch is to work, then a neighbour by
     * country, and finally whoever has been waiting longest.
     */
    private void sortByPromise(List<LobbyClient.Room> board) {
        final String home = (mySite != null) ? mySite : "";
        final String here = LobbySession.country(mm);
        myCountry = here;
        final LobbyClient.Nat self = lastKnownNat();

        java.util.Collections.sort(board, new java.util.Comparator<LobbyClient.Room>() {
            public int compare(LobbyClient.Room a, LobbyClient.Room b) {
                int result = flag(isLocal(b, home)) - flag(isLocal(a, home));
                if (result != 0) return result;

                result = flag(isPlayable(b)) - flag(isPlayable(a));
                if (result != 0) return result;

                result = LobbySession.chanceOf(self, b.nat) - LobbySession.chanceOf(self, a.nat);
                if (result != 0) return result;

                result = flag(sameCountry(b, here)) - flag(sameCountry(a, here));
                if (result != 0) return result;

                result = flag(b.verified) - flag(a.verified);
                if (result != 0) return result;

                /* Longest wait first: they have been sitting there hoping. */
                return Long.compare(a.since, b.since);
            }
        });
    }

    /** Two different countries: the only distance the board can see before
     *  anybody connects, and every cross-border pairing measured so far has
     *  been well past what lockstep can absorb. */
    private boolean farAway(LobbyClient.Room room) {
        String here = myCountry;
        return here != null && room.country != null && !here.equals(room.country);
    }

    private boolean isLocal(LobbyClient.Room room, String home) {
        return home.length() > 0 && home.equals(room.site);
    }

    private boolean isPlayable(LobbyClient.Room room) {
        String title = Emulator.netplayGetDriverDesc(room.game);
        return title != null && title.length() > 0;
    }

    private static boolean sameCountry(LobbyClient.Room room, String here) {
        return here != null && here.equals(room.country);
    }

    private static int flag(boolean value) {
        return value ? 1 : 0;
    }

    /** Mode, input delay and how long the host has been waiting. */
    private String detailOf(LobbyClient.Room room) {
        StringBuilder detail = new StringBuilder();
        detail.append(mm.getString(room.mode == 1
                ? R.string.np_mode_rollback : R.string.np_mode_lockstep));

        /* Delay 0 is not "no delay": it is the Auto setting, and saying zero
         * would promise something the session does not deliver. */
        detail.append(" · ").append(room.delay == 0
                ? mm.getString(R.string.np_lobby_delay_auto)
                : mm.getString(R.string.np_lobby_delay, room.delay));


        /* The room's own code, so a host can say "mine is the K7M2QP4A one"
         * and be found among several of the same game. It identifies the
         * room, never the device: the server makes it at random, it dies
         * with the room, and nothing about the phone goes into it. */
        if (room.id != null && room.id.length() > 0)
            detail.append(" · ").append(mm.getString(R.string.np_lobby_room_code, room.id));
        long waiting = nowSeconds() - room.since;
        if (waiting >= 0)
            detail.append(" · ").append(mm.getString(R.string.np_lobby_waiting, waiting));
        return detail.toString();
    }

    /** Server time, advanced by this device's own uptime since we were told. */
    private long nowSeconds() {
        if (clockSeconds <= 0) return System.currentTimeMillis() / 1000L;
        return clockSeconds + (android.os.SystemClock.elapsedRealtime() - clockTakenAt) / 1000L;
    }

    /**
     * The pitch an empty board makes: what has happened lately, and one thing
     * to do about it. Null when the server sent no figures, its way of saying
     * there is nothing to boast about yet -- silence beats small numbers.
     */
    private View buildShowcase() {
        LobbyClient.Stats recent = showcase;
        if (recent == null) return null;

        float density = mm.getResources().getDisplayMetrics().density;

        LinearLayout box = new LinearLayout(mm);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(0, (int) (14 * density), 0, 0);

        int days = daysSince(recent.since);
        if (recent.rooms > 0 && days > 0)
            box.addView(line(mm.getString(R.string.np_lobby_stats_rooms, recent.rooms, days),
                    14, Color.WHITE));

        if (recent.played > 0)
            box.addView(line(mm.getString(R.string.np_lobby_stats_played, recent.played),
                    13, Color.LTGRAY));

        if (recent.best.length > 0)
            box.addView(line(mm.getString(R.string.np_lobby_stats_best,
                    join(recent.best)), 13, Color.LTGRAY));

        if (recent.games.length > 0)
            box.addView(line(mm.getString(R.string.np_lobby_stats_games,
                    join(recent.games)), 13, Color.LTGRAY));

        if (recent.countries > 0) {
            box.addView(line(mm.getString(R.string.np_lobby_stats_countries, recent.countries),
                    13, Color.LTGRAY));
            /* Its own labelled line: flags trailing the count read as the whole
             * list rather than the busiest of it, and five crowd the row. */
            String flags = flagsOf(recent.flags);
            if (flags.length() > 0)
                box.addView(line(mm.getString(R.string.np_lobby_stats_top, flags),
                        13, Color.LTGRAY));
        }

        /* Suggested off what people finish, not what they open rooms for: a
         * game everyone tries and nobody sees through is the worst thing to
         * recommend. Named even if this build lacks the driver -- the line is
         * a pitch, and whoever wants it can go and find the ROM. */
        String pick = recent.best.length > 0 ? recent.best[0]
                : recent.games.length > 0 ? recent.games[0] : null;

        if (pick != null) {
            TextView call = line(
                    mm.getString(R.string.np_lobby_stats_suggest, pick), 13, Color.CYAN);
            call.setPadding(0, (int) (8 * density), 0, 0);
            box.addView(call);
        }

        return box.getChildCount() > 0 ? box : null;
    }

    private TextView line(String text, int sizeSp, int colour) {
        TextView view = new TextView(mm);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp);
        view.setTextColor(colour);
        view.setText(text);
        return view;
    }

    private static String join(String[] values) {
        StringBuilder out = new StringBuilder();
        for (String value : values) {
            if (out.length() > 0) out.append(", ");
            out.append(value);
        }
        return out.toString();
    }

    /** Whole days covered by the figures, or 0 if the server sent a date this
     *  build cannot read -- left unsaid rather than guessed at. */
    private static int daysSince(String since) {
        if (since == null || since.length() != 10) return 0;
        try {
            java.text.SimpleDateFormat format =
                    new java.text.SimpleDateFormat("yyyy-MM-dd", java.util.Locale.US);
            format.setTimeZone(java.util.TimeZone.getTimeZone("UTC"));
            java.util.Date first = format.parse(since);
            if (first == null) return 0;

            long elapsed = System.currentTimeMillis() - first.getTime();
            /* Inclusive: figures dated today cover one day, not none. */
            int days = (int) (elapsed / 86400000L) + 1;
            return (days >= 1 && days <= 400) ? days : 0;
        } catch (Exception e) {
            return 0;
        }
    }

    /** The busiest countries as flags. A code that will not convert is simply
     *  left out, rather than drawn as a pair of stray boxes. */
    private static String flagsOf(String[] codes) {
        StringBuilder out = new StringBuilder();
        for (String code : codes) {
            String flag = LobbySession.flagOf(code);
            if (flag.length() == 0) continue;
            if (out.length() > 0) out.append(' ');
            out.append(flag);
        }
        return out.toString();
    }

    private View buildRow(final LobbyClient.Room room) {
        float density = mm.getResources().getDisplayMetrics().density;

        /* Empty means this build has no such driver, so the game cannot be
         * played here however good the connection looks. */
        String title = Emulator.netplayGetDriverDesc(room.game);
        final boolean playable = (title != null && title.length() > 0);
        if (!playable) title = room.game;

        boolean local = isLocal(room, (mySite != null) ? mySite : "");
        /* On our own network there is no NAT to defeat, whatever STUN says. */
        int chance = local ? GOOD : LobbySession.chanceOf(lastKnownNat(), room.nat);

        LinearLayout row = new LinearLayout(mm);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(0, (int) (8 * density), 0, (int) (8 * density));

        TextView head = new TextView(mm);
        head.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        head.setText((room.country != null ? "[" + room.country + "]  " : "")
                + title + "  (" + room.game + ")");
        head.setTextColor(playable ? Color.WHITE : Color.GRAY);
        row.addView(head);

        TextView info = new TextView(mm);
        info.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        info.setTextColor(Color.LTGRAY);
        info.setText(detailOf(room));
        row.addView(info);

        Row binding = new Row();
        binding.room = room;
        binding.detail = info;
        visible.add(binding);

        TextView quality = new TextView(mm);
        quality.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        if (!playable) {
            quality.setText(mm.getString(R.string.np_lobby_not_in_build));
            quality.setTextColor(Color.GRAY);
        } else {
            quality.setText(mm.getString(local ? R.string.np_lobby_conn_lan
                    : chance == GOOD ? R.string.np_lobby_conn_good
                    : chance == MAYBE ? R.string.np_lobby_conn_maybe
                    : R.string.np_lobby_conn_bad));
            quality.setTextColor(chance == GOOD ? Color.GREEN
                    : chance == MAYBE ? Color.YELLOW : Color.RED);
        }
        row.addView(quality);


        /* Said as its own line, not folded into the metadata: walking into a
         * game already running is a different thing from waiting for one to
         * start, and it is the first thing to know before tapping. */
        if (room.playing) {
            TextView live = new TextView(mm);
            live.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
            live.setTextColor(Color.CYAN);
            live.setText(mm.getString(R.string.np_lobby_in_progress));
            row.addView(live);
        }
        if (room.locked) {
            TextView locked = new TextView(mm);
            locked.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
            locked.setTextColor(Color.CYAN);
            locked.setText(mm.getString(R.string.np_lobby_private));
            row.addView(locked);
        }

        /* Lockstep waits for every frame and its buffer stops at five of them,
         * so distance turns into stalling rather than into lag. Said before the
         * tap, since the mode cannot be changed once the session is up. */
        if (room.mode == 0 && playable && !local && farAway(room)) {
            TextView slow = new TextView(mm);
            slow.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
            slow.setTextColor(Color.YELLOW);
            slow.setText(mm.getString(R.string.np_lobby_lockstep_far));
            row.addView(slow);
        }

        if (room.plugins) {
            /* Our plugins flag does not travel in the JOIN handshake, so a
             * mismatch can desync with nothing to detect it. Say so. */
            TextView warn = new TextView(mm);
            warn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
            warn.setTextColor(Color.YELLOW);
            warn.setText(mm.getString(R.string.np_lobby_plugins_warn));
            row.addView(warn);
        }

        View separator = new View(mm);
        separator.setBackgroundColor(Color.DKGRAY);
        row.addView(separator, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, (int) density));

        /* A hopeless pairing is shown and refused rather than hidden: a stated
         * reason beats a silent minute of timeout. */
        final boolean joinable = playable && chance != HOPELESS;
        row.setClickable(true);
        row.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                if (!joinable) {
                    netplay.showNetplayError(mm.getString(playable
                            ? R.string.np_lobby_conn_bad_why : R.string.np_lobby_not_in_build));
                    return;
                }
                checkRomThen(room);
            }
        });
        return row;
    }

    /**
     * No ROM, no point talking to the server. The board lists games nobody
     * here owns on purpose -- half its job is telling you which one is worth
     * fetching -- so the check happens on the way in, before the room is
     * claimed. A claimed room that cannot boot is a host waiting for nothing.
     */
    private void checkRomThen(final LobbyClient.Room room) {
        netplay.showBoardProgress(mm.getString(R.string.np_lobby_rom_checking));

        new Thread(new Runnable() {
            public void run() {
                final int found = RomLocator.find(mm, room.game);

                mm.runOnUiThread(new Runnable() {
                    public void run() {
                        netplay.hideBoardProgress();
                        if (found == RomLocator.MISSING) warnNoRom(room);
                        else proceed(room);
                    }
                });
            }
        }).start();
    }

    private void proceed(LobbyClient.Room room) {
        if (room.locked) askPinAndJoin(room); else joinRoom(room, null);
    }

    /**
     * A dead end on purpose. Joining without the ROM claims the room and then
     * fails to boot, which costs the host the slot and a fresh wait: the only
     * thing left to do here is go and fetch the game. Doubt is handled before
     * this point -- an unreadable folder or a custom rompath never gets here.
     */
    private void warnNoRom(LobbyClient.Room room) {
        String title = Emulator.netplayGetDriverDesc(room.game);
        if (title == null || title.length() == 0) title = room.game;

        NetPlayHelper.focusPositive(new AlertDialog.Builder(mm)
                .setTitle(mm.getString(R.string.np_lobby_rom_title))
                .setMessage(mm.getString(R.string.np_lobby_rom_missing, room.game, title))
                .setPositiveButton(mm.getString(R.string.ok), null)
                .show());
    }

    /**
     * A private room asks for its PIN first. The field starts with our own,
     * since a group of friends normally agrees on one and typing it again
     * every time would be the whole feature's undoing.
     */
    private void askPinAndJoin(final LobbyClient.Room room) {
        float density = mm.getResources().getDisplayMetrics().density;

        final android.widget.EditText input = new android.widget.EditText(mm);
        input.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
        input.setText(mm.getPrefsHelper().getNetplayLobbyPin());
        input.setSelection(input.getText().length());

        LinearLayout box = new LinearLayout(mm);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding((int) (20 * density), (int) (8 * density), (int) (20 * density), 0);
        box.addView(input);

        NetPlayHelper.focusPositive(new AlertDialog.Builder(mm)
                .setTitle(mm.getString(R.string.np_lobby_pin_title))
                .setMessage(mm.getString(R.string.np_lobby_pin_prompt))
                .setView(box)
                .setPositiveButton(mm.getString(R.string.ok), new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface d, int w) {
                        joinRoom(room, input.getText().toString().trim());
                    }
                })
                .setNegativeButton(mm.getString(R.string.cancel), null)
                .show());
    }

    /**
     * Claim the room, then hand the address to the join path that already
     * exists. Our own tuple goes up in the same call: punching is symmetric,
     * and without our half the host could never aim back.
     */
    private void joinRoom(final LobbyClient.Room room, final String pin) {
        running = false;
        wake();
        if (dialog != null && dialog.isShowing()) dialog.dismiss();

        final String status = mm.getString(R.string.np_lobby_joining);
        netplay.showBoardProgress(status);

        new Thread(new Runnable() {
            public void run() {
                /* A guess at best, and only worth sending when v4 is the road
                 * we will take: the probe runs on a throwaway socket, so the
                 * port is right only when the NAT preserves it, and it is
                 * v4-only anyway. The real tuple goes up right after our own
                 * STUN; with usable IPv6 that correction is the only one. */
                String mine = null;
                if (!canReachIPv6()) {
                    String probed = Emulator.netplayProbePublicIp();
                    if (probed != null && probed.length() > 0)
                        mine = probed + ":" + netplay.getConfiguredPort();
                }

                List<String> lan = new ArrayList<String>();
                for (String local : netplay.getLocalAddresses())
                    lan.add(local + ":" + netplay.getConfiguredPort());

                LobbyClient.JoinResult result = LobbyClient.join(base, room.id, proto,
                        LobbySession.appVersion(mm), mine, null, lan,
                        LobbySession.natOf(Emulator.netplayGetPublicAddr(), UpnpHelper.isMapped()),
                        LobbySession.country(mm), pin, netplay.claimTokenFor(room.id));

                final String target = (result.host != null) ? pickAddress(result.host) : null;
                /* When the LAN side is the pick, keep the public tuple at
                 * hand: a router that isolates its wireless clients drops LAN
                 * frames between them, and the join then falls back to going
                 * out and back in through the router. */
                final String fallback = (result.host != null && result.host.sameSite
                        && result.host.lan.length > 0)
                        ? pickPublicAddress(result.host) : null;
                final int status = result.status;

                /* The other half of the same diagnosis: what we claimed, what
                 * we told the board about ourselves, and where we are dialing. */
                if (LobbyClient.DEBUG) android.util.Log.d("MAME4droid_Netplay", "lobby: joined " + room.id
                        + " status=" + status + " mine=" + mine + " target=" + target
                        + (result.host != null ? " sameSite=" + result.host.sameSite : ""));

                mm.runOnUiThread(new Runnable() {
                    public void run() {
                        netplay.hideBoardProgress();
                        if (target != null) {
                            /* The join path corrects our tuple once STUN has
                             * run, so it needs to know which room we claimed. */
                            netplay.setLobbyClaim(base, room.id, room.playing, room.game);
                            netplay.setLobbyLanFallback(fallback);
                            netplay.setLobbyPeerInfo(room.country, room.mode, room.delay,
                                    result.host != null && result.host.sameSite,
                                    room.locked,
                                    result.host != null ? result.host.nat : room.nat);
                            netplay.joinGame(target);
                            return;
                        }
                        netplay.showNetplayError(mm.getString(
                                status == 403 ? R.string.np_lobby_bad_pin
                                        : status == 423 ? R.string.np_lobby_pin_blocked
                                        : status == 409 ? R.string.np_lobby_taken
                                        : status == 404 ? R.string.np_lobby_gone
                                        : status == 412 ? R.string.np_lobby_proto
                                        : R.string.np_lobby_join_failed));
                    }
                });
            }
        }).start();
    }

    /**
     * Which of the host's addresses to dial. One public address between us
     * means one router, so the LAN side is the reachable one; that is the same
     * conclusion resolveAndJoin() reaches with an extra STUN probe.
     */
    private String pickAddress(LobbyClient.Endpoint host) {
        if (host.sameSite && host.lan.length > 0)
            return netplay.pickLan(host.lan);

        return pickPublicAddress(host);
    }

    /** The host's dialable public tuple: same v6-reachability rule, no LAN. */
    private String pickPublicAddress(LobbyClient.Endpoint host) {
        boolean primaryIsV6 = host.publicAddr != null && host.publicAddr.startsWith("[");

        /* What we can reach, not what we asked for. On Auto a phone with no
         * IPv6 at all was still handed the host's v6 tuple and answered
         * "network unreachable" -- and the dual-stack host had published its
         * v4 in "alt=" for exactly this. Field-found with two phones. */
        if (primaryIsV6 && !canReachIPv6() && host.publicAlt != null)
            return host.publicAlt;

        return (host.publicAddr != null) ? host.publicAddr : host.publicAlt;
    }

    /**
     * Whether IPv6 is a road we can actually take: allowed by the setting,
     * present now, and proven to work last time we measured. Both are needed
     * -- the scan counts a cellular address while we leave over a v4-only
     * Wi-Fi; the measurement describes the network we played on, not this one.
     */
    private boolean canReachIPv6() {
        if (mm.getPrefsHelper().getNetplayIpProtocol() == 0) return false;
        if (!netplay.hasUsableIPv6()) return false;

        /* Nothing measured that still applies (first session ever, or the
         * family changed): the scan alone, exactly as before. */
        LobbyClient.Nat mine = LobbySession.lastKnownNat(mm);
        return mine == null || mine.v6;
    }

    /** Our own NAT from the last session, or null the first time around. */
    private LobbyClient.Nat lastKnownNat() {
        return LobbySession.lastKnownNat(mm);
    }

    private void postStatus(final String message) {
        mm.runOnUiThread(new Runnable() {
            public void run() {
                if (statusText != null) statusText.setText(message);
            }
        });
    }

    /** Backgrounded: hold the loop where it is, without losing the listing. */
    void pause() {
        paused = true;
    }

    /** Foreground again: refresh at once, so the board is never stale on
     *  screen while it waits out a backoff it started minutes ago. */
    void resume() {
        if (!paused || !running) return;
        paused = false;
        idleRounds = 0;
        wake();
    }

    /* Parked, not spinning: waking it is what Refresh already does. */
    private void awaitResume() {
        synchronized (waiter) {
            try {
                if (paused && running) waiter.wait();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }


    /**
     * Waits out a server-imposed backoff to the very end. Refresh and coming
     * back from the background notify the same monitor, and neither earns
     * another refusal: a spurious wake re-enters the wait. Closing the board
     * still gets out at once, which is the only exit that should work.
     */
    private void sleepThrottled(long ms) {
        long until = android.os.SystemClock.elapsedRealtime() + ms;
        long left;
        while (running && (left = until - android.os.SystemClock.elapsedRealtime()) > 0)
            sleep(left);
    }
    /** Waits out the backoff, but returns at once if Refresh is tapped. */
    private void sleep(long ms) {
        synchronized (waiter) {
            try {
                waiter.wait(ms);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }
}
