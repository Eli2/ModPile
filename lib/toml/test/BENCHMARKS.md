# TOML benchmarks

The `ModPileTomlBenchmark` target measures the parser, ordered document
lookups, document updates, and canonical writer independently. Benchmark cases
use Catch2's `[!benchmark]` tag, so they are not part of normal CTest runs.

Build an optimized executable with debug symbols:

```bash
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-perf --target ModPileTomlBenchmark -j2
```

Run the complete suite:

```bash
./build-perf/lib/toml/test/ModPileTomlBenchmark '[toml]' \
  --benchmark-samples 20
```

Run an individual area:

```bash
./build-perf/lib/toml/test/ModPileTomlBenchmark '[parse]' \
  --benchmark-samples 20
./build-perf/lib/toml/test/ModPileTomlBenchmark '[lookup]' \
  --benchmark-samples 20
./build-perf/lib/toml/test/ModPileTomlBenchmark '[write]' \
  --benchmark-samples 20
```

For a quick smoke run while developing:

```bash
./build-perf/lib/toml/test/ModPileTomlBenchmark '[toml]' \
  --benchmark-samples 3 --benchmark-warmup-time 10 \
  --benchmark-no-analysis
```

Profile one 50,000-key parse without Catch2 benchmark calibration:

```bash
valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes \
  --cachegrind-out-file=/tmp/modpile-toml.cachegrind \
  ./build-perf/lib/toml/test/ModPileTomlBenchmark '[cachegrind-parse]'

cg_annotate --auto=yes \
  --show=Ir,D1mr,D1mw,DLmr,DLmw,Bc,Bcm \
  --sort=Ir /tmp/modpile-toml.cachegrind
```

Compare results only from the same build type and machine. The wide-table and
many-section sizes guard the scaling of `TomlTable` lookup, parser insertion,
and repeated writer updates while serialization continues to exercise ordered
vector iteration.
