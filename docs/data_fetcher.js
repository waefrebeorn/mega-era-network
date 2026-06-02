/**
 * data_fetcher.js — Shared data plumbing for all Money Room pages.
 *
 * Tries localhost:9090 (data_server) first, falls back to relative path.
 * If both fail, returns null so pages show graceful offline state.
 * Data_server availability re-checked every 30s (no permanent cache).
 */
(function(){
  window.DATA_FETCHER = {};

  // Base URL for relative fetches — strips trailing filename if present
  var BASE = (function(){
    var path = window.location.pathname;
    // GitHub Pages serves from /money-room/ subpath
    if (path.startsWith('/money-room')) return '/money-room';
    // For files like /live.html, /pricing.html — stay at root
    return '';
  })();

  var DATA_HOST = null;
  var DATA_CHECKED = 0;  // timestamp of last check, 0 = never
  var CHECK_TTL = 30000; // re-check every 30s

  // Try to reach data_server on localhost:9090
  function tryDataServer(cb) {
    var now = Date.now();
    if (DATA_CHECKED > 0 && (now - DATA_CHECKED) < CHECK_TTL) {
      cb(DATA_HOST);
      return;
    }
    var x = new XMLHttpRequest();
    x.open('GET', 'http://localhost:9090/', true);
    x.timeout = 1500;
    x.onload = function() {
      DATA_HOST = x.status === 200 ? 'http://localhost:9090' : null;
      DATA_CHECKED = Date.now();
      cb(DATA_HOST);
    };
    x.onerror = function() {
      DATA_HOST = null;
      DATA_CHECKED = Date.now();
      cb(null);
    };
    x.ontimeout = function() {
      DATA_HOST = null;
      DATA_CHECKED = Date.now();
      cb(null);
    };
    x.send();
  }

  // Fetch a JSON file with two-tier fallback: data_server → relative path → null
  function fetchJSON(file, cb) {
    tryDataServer(function(host) {
      if (host) {
        var url = host + '/' + file;
        fetch(url, { cache: 'no-store' })
          .then(function(r) {
            if (!r.ok) throw Error('status ' + r.status);
            return r.json();
          })
          .then(function(d) { cb(null, d); })
          .catch(function() {
            // Fall back to relative path on any failure
            fallbackFetch(file, cb);
          });
      } else {
        fallbackFetch(file, cb);
      }
    });
  }

  // Fetch from relative path (GitHub Pages or local file)
  function fallbackFetch(file, cb) {
    var url = BASE + '/data/' + file;
    fetch(url, { cache: 'no-store' })
      .then(function(r) {
        if (!r.ok) throw Error('status ' + r.status);
        return r.json();
      })
      .then(function(d) { cb(null, d); })
      .catch(function() {
        cb(null, null);  // graceful offline — no data available
      });
  }

  // Safe number — returns val if valid, else fallback
  function num(val, fallback) {
    if (val === undefined || val === null) return fallback;
    var n = typeof val === 'string' ? parseFloat(val) : val;
    return isNaN(n) ? fallback : n;
  }

  window.DATA_FETCHER = {
    fetchJSON: fetchJSON,

    // Fetch paper stats and normalize to unified stats object
    fetchStats: function(cb) {
      fetchJSON('paper_stats.json', function(err, ps) {
        if (!ps || !ps.active_agents) {
          cb(err || new Error('no data'), null);
          return;
        }
        var cap = num(ps.avg_capital, 0) * num(ps.active_agents, 0);
        var s = {
          cycle: num(ps.cycle, 0),
          agents: num(ps.active_agents, 0),
          capital: cap,
          trades_total: num(ps.total_trades, 0),
          wins_total: num(ps.total_wins, 0),
          win_rate: num(ps.avg_win_rate, 0.5),
          sharpe: num(ps.sharpe_ratio, 0),
          drawdown: num(ps.drawdown, 0),
          capital_peak: num(ps.peak_capital, cap),
          epoch: num(ps.epoch, 0),
          votes: num(ps.vote_count, 0),
          cycle_time: num(ps.cycle_time_ms, 0),
          timestamp: num(ps.timestamp, 0),
          paper_age: num(ps.timestamp, 0)
        };
        cb(null, s);
      });
    },

    // Fetch prices
    fetchPrices: function(cb) {
      fetchJSON('prices.json', function(err, p) {
        cb(err, p || null);
      });
    },

    // Fetch agents
    fetchAgents: function(cb) {
      fetchJSON('agents.json', function(err, a) {
        cb(err, a || null);
      });
    },

    // Fetch pipeline status
    fetchPipeline: function(cb) {
      fetchJSON('pipeline_status.json', function(err, p) {
        cb(err, p || null);
      });
    },

    // Fetch health
    fetchHealth: function(cb) {
      fetchJSON('health.json', function(err, h) {
        cb(err, h || null);
      });
    },

    // Format helpers
    fmt: function(n, d) {
      n = num(n, null);
      if (n === null) return '\u2014';
      return n.toLocaleString(undefined, {minimumFractionDigits: d || 0, maximumFractionDigits: d || 2});
    },
    pct: function(n) {
      n = num(n, null);
      if (n === null) return '\u2014';
      return (n * 100).toFixed(1) + '%';
    },
    ago: function(ts) {
      if (!ts) return '\u2014';
      var d = Math.floor(Date.now() / 1000 - ts);
      if (d < 0) return 'just now';
      if (d < 60) return d + 's';
      if (d < 3600) return Math.floor(d / 60) + 'm';
      return Math.floor(d / 3600) + 'h';
    }
  };
})();
