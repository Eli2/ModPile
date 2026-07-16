# Import/export benchmarking

Run these commands from the repository root. The benchmark and profiler
workloads live in `test/test_table_import_export_performance.cpp` and are hidden
from normal Catch2 test runs with the `[!benchmark]` tag.

## Prerequisites on Ubuntu

```bash
sudo apt update
sudo apt install valgrind linux-tools-common linux-tools-generic
```

`libc6-dbg` improves symbol resolution inside libc. `kcachegrind` is an optional
graphical viewer for Cachegrind and Callgrind output.

```bash
sudo apt install libc6-dbg
# Optional:
sudo apt install kcachegrind
```

Hardware counters may be disabled even when `perf` is installed. Check with:

```bash
sysctl kernel.perf_event_paranoid
```

If `perf stat` reports a permission error, temporarily allow user-space
profiling:

```bash
sudo sysctl -w kernel.perf_event_paranoid=0
```

Restore the previous value after profiling if required by the system's security
policy.

## Build

Use an optimized build with debug symbols for profiling:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j2
```

Confirm the effective flags before comparing results:

```bash
grep -E '^CMAKE_BUILD_TYPE:|^CMAKE_CXX_FLAGS_RELWITHDEBINFO:' build/CMakeCache.txt
```

The usual `RelWithDebInfo` configuration uses `-O2 -g`. A separate `-O3` build
can be created without changing the normal build directory:

```bash
cmake -S . -B build-o3 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO='-O3 -g -DNDEBUG'
cmake --build build-o3 --target ModPileTest -j2
```

## Wall-clock benchmarks

Run the general scaling and BLOB benchmarks with:

```bash
./build/ModPileTest 'generic table import export performance'
```

Use fewer samples for a quick run:

```bash
./build/ModPileTest 'generic table import export performance' \
  --benchmark-samples 5 --benchmark-warmup-time 20
```

The million-row workloads are isolated by tag:

```bash
./build/ModPileTest '[perf-import]' --benchmark-samples 5 --benchmark-warmup-time 20
./build/ModPileTest '[perf-sqlite]' --benchmark-samples 5 --benchmark-warmup-time 20
./build/ModPileTest '[perf-export]' --benchmark-samples 5 --benchmark-warmup-time 20
```

Compare `[perf-import]` with `[perf-sqlite]` to estimate how much time belongs to
parsing and validation rather than SQLite insertion.

## Hardware counters with perf

Use the direct workload for profiling. It performs one million-row import
without Catch2 benchmark calibration and statistical analysis around it:

```bash
perf stat -r 5 \
  -e cycles:u,instructions:u,branches:u,branch-misses:u,\
L1-dcache-loads:u,L1-dcache-load-misses:u \
  ./build/ModPileTest '[perf-import-direct]'
```

Useful ratios are:

- IPC: `instructions / cycles`;
- branch-miss rate: `branch-misses / branches`;
- L1 data miss rate: `L1-dcache-load-misses / L1-dcache-loads`.

Some virtualized PMUs report zero or unsupported generic cache events. Treat
those values as unavailable and use Cachegrind for cache analysis.

For native cycle attribution and call stacks:

```bash
perf record -e cycles:u -g --call-graph dwarf \
  -o /tmp/modpile-import.perf.data -- \
  ./build/ModPileTest '[perf-import-direct]'

perf report -i /tmp/modpile-import.perf.data
```

A text report is convenient for comparisons:

```bash
perf report --stdio --no-children --percent-limit 0.5 \
  -i /tmp/modpile-import.perf.data --sort dso,symbol
```

Pinning runs to one otherwise-idle CPU can reduce noise:

```bash
taskset -c 2 perf stat -r 10 -e task-clock,cycles:u,instructions:u \
  ./build/ModPileTest '[perf-import-direct]'
```

Choose a CPU that exists and is not busy on the machine being tested.

## Cache and simulated branch analysis

Cachegrind is much slower than native execution, so its dedicated workload uses
100,000 rows and avoids Catch2 benchmark calibration:

```bash
valgrind --tool=cachegrind \
  --cache-sim=yes --branch-sim=yes \
  --cachegrind-out-file=/tmp/modpile-import.cachegrind \
  ./build/ModPileTest '[cachegrind-import]'
```

Inspect the hottest functions and source lines with:

```bash
cg_annotate --auto=yes \
  --show=Ir,D1mr,D1mw,DLmr,DLmw,Bc,Bcm \
  --sort=D1mr /tmp/modpile-import.cachegrind

cg_annotate --auto=yes \
  --show=Ir,D1mr,D1mw,DLmr,DLmw,Bc,Bcm \
  --sort=Bcm /tmp/modpile-import.cachegrind
```

The important event names are:

- `Ir`: instructions executed;
- `D1mr`/`D1mw`: L1 data read/write misses;
- `DLmr`/`DLmw`: last-level data read/write misses;
- `Bc`: conditional branches;
- `Bcm`: simulated conditional branch mispredictions.

Cachegrind uses a simplified branch predictor. Prefer real `perf` branch-miss
measurements when deciding whether to add `[[likely]]` or `[[unlikely]]`.
Cachegrind execution time is instrumentation overhead and must not be used as a
performance result.

## Callgrind

For deterministic instruction-level call costs:

```bash
valgrind --tool=callgrind \
  --callgrind-out-file=/tmp/modpile-import.callgrind \
  ./build/ModPileTest '[cachegrind-import]'

callgrind_annotate --inclusive=yes /tmp/modpile-import.callgrind
```

Open the same file with `kcachegrind` if installed:

```bash
kcachegrind /tmp/modpile-import.callgrind
```

## Comparing a change

1. Use the same binary configuration, CPU, workload, and sample count.
2. Record a baseline before editing.
3. Change one thing at a time.
4. Run the normal test suite after each candidate change:

   ```bash
   ./build/ModPileTest
   ```

5. Keep a change only when repeated native timings improve outside the reported
   variance. Use `perf` or Cachegrind to explain the improvement.

Do not add branch hints solely from source intuition or Cachegrind's simulated
predictor. In previous profiling, native hardware branch misses were already
well below one percent while Cachegrind estimated a much higher rate.
