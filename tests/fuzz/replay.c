/* Runs saved fuzzer inputs (the seed corpus and any crash regressions)
 * through the harness without libFuzzer, so they are ordinary tests on every
 * platform. Usage: fuzz_replay FILE... */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int main(int argc, char **argv) {
    int files = 0;
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            perror(argv[i]);
            return 1;
        }
        uint8_t *buf = malloc(1 << 20);
        size_t n = buf ? fread(buf, 1, 1 << 20, f) : 0;
        fclose(f);
        if (!buf) return 1;
        LLVMFuzzerTestOneInput(buf, n);
        free(buf);
        files++;
    }
    printf("replayed %d inputs\n", files);
    return 0;
}
