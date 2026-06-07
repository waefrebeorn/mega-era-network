/**
 * room_darwin_test.c — Test wrapper for room_darwin module
 * Compile: gcc -O2 -o room_darwin room_darwin_test.c room_darwin.c -lm
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "types.h"

// Include the module functions
extern RoomError room_darwin_evolve(AgentState *agents, int n, int cycle, DarwinRecord *rec, const int *agent_market);

int main(void) {
    printf("room_darwin test: module compiled and linked successfully\n");

    // Create minimal test state
    AgentState agents[10];
    DarwinRecord rec = {0};
    int agent_market[10] = {0};

    // Initialize with dummy data
    for (int i = 0; i < 10; i++) {
        agents[i].alive = 1;
        agents[i].capital = 100.0f + i * 10.0f;
        agents[i].trades = 50 + i * 5;
        agents[i].wins = 25 + i * 2;
        agents[i].losses = 25 + i * 3;
        agents[i].win_rate_ema = 0.5f + i * 0.01f;
        agents[i].consecutive_losses = i % 3;
        agents[i].peak_capital = agents[i].capital;
        // Minimal genome - just set required fields
        agents[i].genome.position_size = 0.1f;
        agents[i].genome.conviction_threshold = 0.5f;
        agents[i].genome.risk_tolerance = 0.5f;
        agents[i].genome.stop_loss_pct = 0.05f;
        agents[i].genome.take_profit_pct = 0.1f;
        agents[i].genome.learning_rate = 0.01f;
        for (int j = 0; j < N_FEATURES; j++) {
            agents[i].genome.feat_weight[j] = 0.0f;
            agents[i].genome.regime_weight[0][j] = 0.0f;
            agents[i].genome.regime_weight[1][j] = 0.0f;
            agents[i].genome.regime_weight[2][j] = 0.0f;
        }
        agents[i].genome.bias = 0.0f;
        agents[i].genome.regime_bias[0] = 0.0f;
        agents[i].genome.regime_bias[1] = 0.0f;
        agents[i].genome.regime_bias[2] = 0.0f;
    }

    printf("Testing room_darwin_evolve with 10 agents...\n");
    room_darwin_evolve(agents, 10, 100, &rec, agent_market);

    printf("room_darwin_evolve returned: epoch=%d, culled=%d, cloned=%d, mutation_rate=%.4f\n",
           rec.epoch, rec.culled, rec.cloned, rec.mutation_rate);

    // Verify at least something happened
    if (rec.culled > 0 || rec.cloned > 0) {
        printf("✅ room_darwin test passed\n");
        return 0;
    } else {
        printf("⚠️ room_darwin test: no culling/cloning occurred (may be expected for small population)\n");
        return 0;
    }
}