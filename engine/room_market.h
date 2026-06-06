#ifndef ROOM_MARKET_H
#define ROOM_MARKET_H

#include "types.h"

// Execute trades against market maker (replaces P2P room_capital_apply)
RoomError room_market_apply(VoteRecord *votes, int count,
                            AgentState *agents, int n,
                            TradeRecord *trades, int start_offset,
                            int *new_count, int64_t window_ts);

// Resolve market maker trades (replaces P2P room_capital_resolve)
RoomError room_market_resolve(TradeRecord *trades, int *tcount,
                              const MarketTick *tick,
                              float prev_close,
                              AgentState *agents,
                              int max_trades,
                              float *room_capital);

// P2P Room capital functions (used by main engine)
RoomError room_capital_apply(VoteRecord *votes, int count,
                             AgentState *agents, int n_unused,
                             TradeRecord *trades, int start_offset,
                             int *new_count, int64_t window_ts,
                             int predicted_regime,
                             RoomState *s);

RoomError room_capital_resolve(TradeRecord *trades, int *tcount,
                               const MarketTick *resolution_tick,
                               float prev_close,
                               AgentState *agents,
                               int max_trades,
                               FeatureImportance *importance,
                               float lr_decay,
                               RoomState *s);

// Print market maker stats
void room_market_stats(RoomState *state);

#endif // ROOM_MARKET_H
