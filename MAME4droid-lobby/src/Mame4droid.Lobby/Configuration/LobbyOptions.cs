/*
 * This file is part of MAME4droid (NetPlay lobby server).
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
 */

namespace Mame4droid.Lobby.Configuration;

/// Everything tunable at runtime lives here: appsettings.json is reloaded on
/// change, so cadences and TTLs can be adjusted without publishing an APK.
/// Rate limits are the exception -- they are read once at startup.
public sealed class LobbyOptions
{
    public const string SectionName = "Lobby";

    /* Kill switch: when false only GET /config answers, everything else 503s. */
    public bool Enabled { get; set; } = true;
    public string Notice { get; set; } = "";
    public string MinApp { get; set; } = "";

    /* Client cadences, in seconds. */
    public int PollSeconds { get; set; } = 3;
    public int ListSeconds { get; set; } = 5;

    /* Adaptive list polling: the client multiplies ListSeconds by ListBackoff
     * after every 304 and clamps at ListMaxSeconds, resetting on any 200 or on
     * user interaction.  Server-driven so it can be tightened under load. */
    public double ListBackoff { get; set; } = 1.5;
    public int ListMaxSeconds { get; set; } = 20;

    /* Room lifetime. A claim has to outlive at least one host heartbeat (three
     * seconds waiting, ten in drop-in) so the host learns who joined, and die
     * well before the joiner's own 30s patience runs out -- at equal values the
     * two raced, and a joiner's lapsed claim locked it out of its own retry. */
    public int OpenTtlSeconds { get; set; } = 60;
    public int ClaimedTtlSeconds { get; set; } = 20;

    /* Abuse limits. */

    /* Rooms one public address may hold at once. Not one per player: a house,
     * a LAN party or an office all publish from a single address, and at 2 the
     * third person to press Create was refused with nothing on screen to say
     * why. High enough for a room full of people, low enough that one address
     * cannot fill the board. */
    public int MaxRoomsPerIp { get; set; } = 8;

    public int MaxLanTuples { get; set; } = 4;
    public int MaxRoomsListed { get; set; } = 100;
    public int MaxBodyBytes { get; set; } = 1024;

    /* Wrong PINs a private room tolerates before it stops accepting joins.
     * Low on purpose: a friend mistypes once, an attacker needs thousands. */
    public int MaxPinAttempts { get; set; } = 5;

    /* Refuse a room whose advertised tuple contradicts the observed HTTP IP in
     * the same family.  Softening this to a verified:false badge is a one-line
     * config change if CGNAT pools turn out to hand different IPs per flow. */
    public bool RejectUnverified { get; set; } = true;

    /* Temporary: exposes GET /api/v1/whoami, which reports what the platform in
     * front of us says about the caller. Leave off in normal operation. */
    public bool Diagnostics { get; set; }

    /* Only for self-hosting behind your OWN reverse proxy, and only when that
     * proxy is the sole way in: it has the framework rewrite the caller from
     * X-Forwarded-For. Must stay false anywhere the host resolves the caller
     * before the app sees it -- there, one header would buy any address. */
    public bool TrustForwardedHeaders { get; set; }

    /* Sliding window used for the "N players browsing" line in the host dialog. */
    public int ViewerWindowSeconds { get; set; } = 30;

    /* The showcase an empty board draws. A rolling window, not a running total,
     * so it describes the board as it is now. Wide while the board is young;
     * narrow it once a single week stands up on its own. */
    public int StatsWindowDays { get; set; } = 30;
    public int StatsTopGames { get; set; } = 3;
    /* Countries get a line of their own on the board, so the row has space for
     * many more than the games ranking does -- as many as there are, up to ten. */
    public int StatsTopCountries { get; set; } = 10;

    /* Most played gets a longer list of its own: it is a leaderboard, not a
     * flourish, and a name only earns a place there by being finished. One is
     * enough to show -- unlike a ranking of wishes, a single one still means
     * somebody played it through. */
    public int StatsTopBest { get; set; } = 10;
    public int StatsMinBest { get; set; } = 1;

    /* How often the counters reach the disk. Everything is served from memory,
     * so this only trades how much a restart forgets against how much work the
     * instance does while otherwise idle. */
    public int StatsSaveSeconds { get; set; } = 300;

    /* Counted and kept but not sent: beside the rooms figure it reads as a gulf,
     * because a room counts when it opens and a game only once two people
     * finished one. Turn on when the two can stand side by side. */
    public bool StatsShowPlayed { get; set; } = false;

    /* "Most wanted" -- every time a name came up, room opened or game finished.
     * Held back the same way: what people actually finished is the better
     * recommendation, and two lists of games at once is one too many. */
    public bool StatsShowGames { get; set; } = false;

    /* Nothing under these is shown at all. A figure that reads as small does
     * the opposite of what the showcase is for, so "1 country" and "3 rooms"
     * are better left unsaid than said honestly. */
    public int StatsMinRooms { get; set; } = 10;
    public int StatsMinPlayed { get; set; } = 3;
    public int StatsMinCountries { get; set; } = 3;
    public int StatsMinGames { get; set; } = 3;

    public RateLimitOptions RateLimits { get; set; } = new();
}

/// Requests per minute, one bucket per endpoint. The bucket is keyed by public
/// address, so a household, a LAN party or an office all share a single budget
/// -- these numbers have to fit several players on one router, not one player.
/// The daily quota is defended elsewhere (kill switch, ETag/304), so these are
/// abuse ceilings, well above what normal use spends.
public sealed class RateLimitOptions
{
    public int Config { get; set; } = 30;

    /// Its own bucket: it touches no state and returns three bytes, and it is
    /// what warms a sleeping instance. Sharing Config's budget meant opening
    /// the netplay dialog could spend the board's own call.
    public int Health { get; set; } = 60;

    public int List { get; set; } = 120;
    public int Create { get; set; } = 20;
    public int Join { get; set; } = 30;
    public int Poll { get; set; } = 180;
    public int Telemetry { get; set; } = 20;
}
