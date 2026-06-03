/**
 * A45: Feature correlation matrix
 * Reads room_state.bin, computes pairwise Pearson correlation
 * between all N_FEATURES features across agents.
 * High correlation → redundant features → PCA/decorrelation candidate.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "types.h"

#define CORR_THRESH 0.85f  // Flag pairs above this

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "room_state.bin";
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "Cannot open %s\n", path); return 1; }
    struct stat st; fstat(fd, &st);
    RoomState *state = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (!state || state->magic != STATE_MAGIC) {
        fprintf(stderr, "Invalid state file\n"); return 1;
    }

    // Collect features across all alive agents
    int n = 0;
    float feat_buf[MAX_AGENTS][N_FEATURES];
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (!state->agents[i].alive || state->agents[i].trades < 10) continue;
        for (int f = 0; f < N_FEATURES; f++)
            feat_buf[n][f] = state->agents[i].last_features[f];
        n++;
    }
    if (n < 10) { printf("Not enough agents (%d) for correlation analysis\n", n); return 0; }
    printf("Computing feature correlation matrix: %d features x %d agents\n", N_FEATURES, n);

    // Compute means and stddevs
    float mean[N_FEATURES], std[N_FEATURES];
    for (int f = 0; f < N_FEATURES; f++) {
        mean[f] = 0;
        for (int i = 0; i < n; i++) mean[f] += feat_buf[i][f];
        mean[f] /= n;
        float var = 0;
        for (int i = 0; i < n; i++) var += (feat_buf[i][f] - mean[f]) * (feat_buf[i][f] - mean[f]);
        std[f] = sqrtf(var / n);
    }

    // Compute pairwise correlations and flag high-correlation pairs
    int redundant = 0;
    for (int a = 0; a < N_FEATURES; a++) {
        for (int b = a + 1; b < N_FEATURES; b++) {
            if (std[a] < 1e-6f || std[b] < 1e-6f) continue;
            float cov = 0;
            for (int i = 0; i < n; i++)
                cov += (feat_buf[i][a] - mean[a]) * (feat_buf[i][b] - mean[b]);
            cov /= n;
            float corr = cov / (std[a] * std[b]);
            if (fabsf(corr) > CORR_THRESH) {
                printf("  REDUNDANT: feat[%d] ↔ feat[%d] corr=%+.3f\n", a, b, corr);
                redundant++;
            }
        }
    }
    printf("\n%d redundant pairs (|corr| > %.2f) out of %d total\n",
           redundant, CORR_THRESH, N_FEATURES * (N_FEATURES - 1) / 2);
    printf("Verdict: %s\n", redundant > 5 ? "PCA recommended" : "Feature set is sufficiently decorrelated");

    munmap(state, st.st_size);
    return 0;
}
