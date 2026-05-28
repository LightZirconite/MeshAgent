/*
 * Alternative desktop capability helper.
 *
 * This module intentionally avoids Windows RDP hacks or hidden-control tricks.
 * It exposes the real platform capability so the server/UI can choose a clear
 * path: Linux virtual KVM when available, or an explicit unsupported state.
 */

function safeRequire(name)
{
    try { return require(name); } catch (e) { return null; }
}

function isRootOrAdmin()
{
    try
    {
        if (process.platform == 'win32') { return true; }
        return (process.getuid && process.getuid() == 0);
    }
    catch (e) { return false; }
}

function getLinuxCapabilities()
{
    var kvm = safeRequire('kvm-helper');
    var users = {};
    var diagnostics = {};
    var spawnable = [];

    if (kvm == null)
    {
        return {
            state: 'unavailable',
            mode: null,
            reason: 'kvm-helper unavailable',
            diagnostics: diagnostics,
            sessions: []
        };
    }

    try { diagnostics = kvm.diagnostics ? kvm.diagnostics() : {}; } catch (e) { diagnostics = { error: '' + e }; }
    try { users = kvm.users ? kvm.users() : {}; } catch (e) { users = {}; diagnostics.usersError = '' + e; }

    Object.keys(users).forEach(function (id) {
        var session = users[id];
        if (session && session.State == 'Spawnable')
        {
            spawnable.push({
                id: parseInt(id),
                username: session.Username,
                stationName: session.StationName,
                state: session.State
            });
        }
    });

    if (kvm.hasVirtualSessionSupport && spawnable.length > 0)
    {
        return {
            state: 'available',
            mode: 'linux-xvfb',
            reason: diagnostics && diagnostics.wayland && diagnostics.wayland.sessionType == 'wayland' ?
                'Linux virtual Xvfb desktop is available while the physical session is Wayland' :
                'Linux virtual desktop is available',
            diagnostics: diagnostics,
            sessions: spawnable
        };
    }

    return {
        state: 'setup-required',
        mode: 'linux-xvfb',
        reason: 'Linux virtual desktop needs xvfb-run and a supported desktop environment',
        diagnostics: diagnostics,
        sessions: spawnable,
        setup: {
            automatic: false,
            packages: ['xvfb', 'gnome-session | lxde | xfce4'],
            rebootRequired: false
        }
    };
}

function getWindowsCapabilities()
{
    return {
        state: 'unavailable',
        mode: null,
        reason: 'Windows alternative desktop is not implemented as an explicit audited session backend',
        diagnostics: {
            platform: process.platform,
            admin: isRootOrAdmin(),
            hiddenDesktop: false,
            rdpClassic: false,
            vmPreferredFallback: false
        }
    };
}

function capabilities()
{
    if (process.platform == 'linux')
    {
        var linux = getLinuxCapabilities();
        linux.platform = process.platform;
        linux.admin = isRootOrAdmin();
        return linux;
    }

    if (process.platform == 'win32')
    {
        return getWindowsCapabilities();
    }

    return {
        platform: process.platform,
        admin: isRootOrAdmin(),
        state: 'unavailable',
        mode: null,
        reason: 'Alternative desktop is not implemented for this platform',
        diagnostics: { platform: process.platform }
    };
}

function setup()
{
    var caps = capabilities();
    caps.setupAttempted = true;
    caps.setupChangedSystem = false;
    return caps;
}

function start(options)
{
    options = options || {};
    var caps = capabilities();

    if (process.platform == 'linux' && caps.mode == 'linux-xvfb' && caps.state == 'available')
    {
        var kvm = require('kvm-helper');
        var sessionId = options.sessionId;
        if (sessionId == null && caps.sessions && caps.sessions.length > 0) { sessionId = caps.sessions[0].id; }
        if (sessionId == null) { throw ('No spawnable Linux virtual session found'); }
        return {
            ok: true,
            mode: 'linux-xvfb',
            tsid: kvm.createVirtualSession(sessionId),
            reason: 'Linux virtual desktop started'
        };
    }

    if (process.platform == 'win32')
    {
        return {
            ok: false,
            mode: caps.mode,
            reason: caps.reason
        };
    }

    return {
        ok: false,
        mode: caps.mode,
        reason: caps.reason
    };
}

function stop()
{
    return {
        ok: true,
        reason: 'No dedicated alternative desktop cleanup is required by this helper'
    };
}

module.exports = {
    capabilities: capabilities,
    setup: setup,
    start: start,
    stop: stop
};
