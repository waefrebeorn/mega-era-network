/**
 * data_fetcher.js — Shared data plumbing for all Money Room pages.
 *
 * Tries localhost:9090 (data_server) first, falls back to relative path.
 * Maps field names from data_server endpoints to what UI expects.
 */
(function(){
  window.DATA_FETCHER = {};

  // Base URL resolution
  var BASE = (function(){
    var path = window.location.pathname;
    if (path.startsWith('/money-room')) return '/money-room';
    return '';
  })();

  // Try to reach data_server on localhost:9090
  var DATA_HOST = null;
  var DATA_CHECKED = false;

  function tryDataServer(cb) {
    if (DATA_CHECKED) { cb(DATA_HOST); return; }
    var x = new XMLHttpRequest();
    x.open('GET', 'http://localhost:9090/', true);
    x.timeout = 2000;
    x.onload = function() {
      if (x.status === 200) {
        DATA_HOST = 'http://localhost:9090';
      } else {
        DATA_HOST = null;
      }
      DATA_CHECKED = true;
      cb(DATA_HOST);
    };
    x.onerror = function() {
      DATA_HOST = null;
      DATA_CHECKED = true;
      cb(null);
    };
    x.ontimeout = function() {
      DATA_HOST = null;
      DATA_CHECKED = true;
      cb(null);
    };
    x.send();
  }

  // Fetch a JSON file, trying data_server first, then relative path
  function fetchJSON(file, cb) {
    tryDataServer(function(host) {
      if (host) {
        var url = host + '/' + file;
        fetch(url, { cache: 'no-store' })
          .then(function(r) { if (!r.ok) throw Error('status ' + r.status); return r.json(); })
          .then(function(d) { cb(null, d); })
          .catch(function(e) { cb(e); });
      } else {
        var url = BASE + '/data/' + file;
        fetch(url, { cache: 'no-store' })
          .then(function(r) { if (!r.ok) throw Error('status ' + r.status); return r.json(); })
          .then(function(d) { cb(null, d); })
          .catch(function(e) { cb(e); });
      }
    });
  }

  // Field aliases: source_field → target_field. Handle both naming conventions.
  function aliasFields(src, dst, mapping) {
    for (var key in mapping) {
      if (src[key] !== undefined) {
        dst[mapping[key]] = src[key];
      }
    }
    return dst;
  }

  window.DATA_FETCHER = {
    fetchJSON: fetchJSON,
    tryDataServer: tryDataServer,

    // Fetch paper stats and normalize to unified stats object
    fetchStats: function(cb) {
      fetchJSON('paper_stats.json', function(err, ps) {
        if (err || !ps) {
          cb(err, null);
          return;
        }
        var s = {};
        s.cycle = ps.cycle || 0;
        s.agents = ps.active_agents || 0;
        s.capital = ps.avg_capital * ps.active_agents;
        s.trades_total = ps.total_trades || 0;
        s.win_rate = ps.avg_win_rate || 0.5;
        s.sharpe = ps.sharpe_ratio || 0;
        s.capital_peak = 0; // not in paper_stats
        s.epoch = 0; // not in paper_stats
        s.votes = 0; // not in paper_stats
        s.cycle_time = 0;
        s.drawdown = 0;
        s.timestamp = ps.timestamp || 0;
        s.paper_age = ps.timestamp || 0;
        cb(null, s);
      });
    },

    // Fetch prices
    fetchPrices: function(cb) {
      fetchJSON('prices.json', function(err, p) {
        cb(err, p);
      });
    },

    // Fetch agents
    fetchAgents: function(cb) {
      fetchJSON('agents.json', function(err, a) {
        cb(err, a);
      });
    },

    // Fetch pipeline status
    fetchPipeline: function(cb) {
      fetchJSON('pipeline_status.json', function(err, p) {
        cb(err, p);
      });
    },

    // Fetch health
    fetchHealth: function(cb) {
      fetchJSON('health.json', function(err, h) {
        cb(err, h);
      });
    },

    // Format helpers
    fmt: function(n, d) {
      if (n === undefined || n === null) return '\u2014';
      if (typeof n === 'string') n = parseFloat(n);
      if (isNaN(n)) return '\u2014';
      return n.toLocaleString(undefined, {minimumFractionDigits: d || 0, maximumFractionDigits: d || 2});
    },
    pct: function(n) {
      if (n === undefined || n === null) return '\u2014';
      n = typeof n === 'string' ? parseFloat(n) : n;
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
