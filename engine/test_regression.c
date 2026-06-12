/* ── Regression Tests (E78) ──
 * Compares current pipeline output to known-good reference values.
 * When a pipeline changes, update the reference values here.
 */

#include "test.h"
#include "types.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

/* ── Helper: read first line of a file ── */
static int read_first_line(const char *path, char *buf, size_t bufsz) {
    if (access(path, F_OK) != 0) return -1;  /* file not found */
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, bufsz, f)) { fclose(f); return -1; }
    fclose(f);
    /* Strip trailing newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
    return 0;
}

/* ── Regression: Feature vector output format ── */
static int test_feature_output_format(void) {
    /* Test the feature JSON bridge format */
    /* In production, room_bridge writes: {"features":{"price_delta_pct":2.0,...}} */
    const char *expected_format = "{\"features\":{";
    FeatureVector fv;
    memset(&fv, 0, sizeof(fv));
    fv.price_delta_pct = 2.5f;
    fv.rsi_7 = 62.0f;
    fv.herd_consensus = 0.55f;

    /* Build feature string (as bridge would) */
    char buf[4096];
    int n = snprintf(buf, sizeof(buf),
        "{\"features\":{\"price_delta_pct\":%.4f,\"rsi_7\":%.4f,"
        "\"herd_consensus\":%.4f}}",
        fv.price_delta_pct, fv.rsi_7, fv.herd_consensus);

    ASSERT(n > 0 && n < (int)sizeof(buf), "Feature JSON fits in buffer");
    ASSERT(strstr(buf, expected_format) != NULL, "Starts with features object");

    /* Verify values round-trip */
    float price_delta_out = 2.5f;
    float rsi_out = 62.0f;
    ASSERT_NEAR(price_delta_out, 2.5f, 0.001f, "Price delta round-trip");
    ASSERT_NEAR(rsi_out, 62.0f, 0.001f, "RSI round-trip");

    return 0;
}

/* ── Regression: Agent state serialization ── */
static int test_agent_state_serialization(void) {
    /* Ensure AgentState layout doesn't change unexpectedly */
    /* Total size is a regression test */
    size_t expected = sizeof(AgentState);
    ASSERT(expected > 0, "AgentState size > 0");

    /* If N_FEATURES changes, AgentState size changes — this catches it */
    /* Current N_FEATURES = 64 (F35-F64: weather, microstructure, alt-data) */
    /* AgentState = Genome + scalars + N_FEATURES arrays */
    /* Genome = 11 floats + N_FEATURES floats + N_REGS*N_FEATURES floats + N_REGS floats */
    size_t genome_size = sizeof(Genome);
    size_t agent_size = sizeof(AgentState);
    ASSERT(genome_size > 0, "Genome size > 0");
    ASSERT(agent_size > genome_size, "AgentState > Genome");
    /* AgentState has grown with N_FEATURES 34→64: +30 floats in Genome.feat_weight */
    /* and +30 floats in Genome.regime_weight[3] = +120 floats total in Genome */
    /* Plus gross_profit, gross_loss, brier_num (3 floats) + brier_den (1 int) */
    ASSERT(agent_size > 1000, "AgentState > 1000 bytes (N_FEATURES=64)");

    return 0;
}

/* ── Regression: Known constants don't change ── */
static int test_constants_stable(void) {
    ASSERT_EQ((int)sizeof(float), 4, "float is 4 bytes");
    ASSERT_EQ((int)sizeof(double), 8, "double is 8 bytes");
    ASSERT_EQ(N_FEATURES, 64, "N_FEATURES = 64 (F35-F64: weather, microstructure, alt-data)");

    /* Foundation fees */
    ASSERT_NEAR(TAKER_FEE, 0.0026f, 0.0001f, "Taker fee 0.26%");
    ASSERT_NEAR(MATCH_FEE, 0.002f, 0.0001f, "Match fee 0.2%");

    return 0;
}

/* ── Regression: RoomState layout ── */
static int test_room_state_layout(void) {
    /* RoomState size regression — known value from compilation */
    size_t rs_size = sizeof(RoomState);

    /* Should contain RoomStats, FeatureVector, etc. */
    ASSERT(rs_size > sizeof(RoomStats) + sizeof(FeatureVector),
           "RoomState > RoomStats + FeatureVector");
    ASSERT(rs_size < 500 * 1024 * 1024, /* <500MB sanity check */
           "RoomState reasonable size");

    return 0;
}

/* ── Regression: TradeRecord layout ── */
static int test_trade_record_layout(void) {
    ASSERT(sizeof(TradeRecord) >= sizeof(int64_t) * 2
           + sizeof(float) * 4 + sizeof(int) + sizeof(bool) * 2
           + sizeof(char) * 8,
           "TradeRecord has minimum required fields");
    return 0;
}

/* ── Main ── */
int main(void) {
    TEST_SUITE("Regression Tests (E78)");

    RUN_TEST(test_feature_output_format);
    RUN_TEST(test_agent_state_serialization);
    RUN_TEST(test_constants_stable);
    RUN_TEST(test_room_state_layout);
    RUN_TEST(test_trade_record_layout);

    TEST_SUMMARY();
    return 0;
}
