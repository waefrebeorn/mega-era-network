╔══════════════════════════════════════════════════════════════╗
║  Timeline DB Architecture                                   ║
║  ─────────────────────────                                   ║
║  Two SQLite DBs carry timeline data:                        ║
║                                                            ║
║  1. ~/.hermes/pm_logs/timeline.db (9.3 GB)                 ║
║     — "Main" DB with most collectors writing here           ║
║     — Tables: timeline, timeline_hourly, forex_rates,       ║
║       news_headlines, sentiment_*, weather_daily,           ║
║       dividends, stock_screener, dexscreener_pairs,         ║
║       defillama_tvl                                         ║
║     — timeline table: 18.6M rows from 550K+ sources         ║
║     — Used by: timeline_aggregator, cross_asset_c,          ║
║       data_pipeline, market_microstructure                  ║
║                                                            ║
║  2. engine/timeline.db (engine-local, small)                ║
║     — Tables: blockchain_data, fear_greed, fred_data,       ║
║       sports_data, timeline (synced via timeline_sync)       ║
║     — timeline table: synced from main DB every 5 min       ║
║     — Local tables written by engine-specific collectors:   ║
║       blockchain_com_collector, fear_greed_collector,        ║
║       fred_collector, sports_collector                      ║
║                                                            ║
║  SYNC: ./timeline_sync (cron: */5 * * * *) copies new      ║
║  timeline rows from main DB → engine DB so queries          ║
║  against engine/timeline.db's timeline table return data.   ║
║                                                            ║
║  IDEAL FIX: unify paths — make all collectors write to      ║
║  a single DB. Requires updating ~30 collector .c files       ║
║  that hardcode DB_PATH. Marked P2 (non-blocking).           ║
╚══════════════════════════════════════════════════════════════╝
