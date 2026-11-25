# Flight Log Inspector

Flight Log Inspector reads recorded ULog (`.ulg`) flight telemetry. Use the command-line tool
to inspect topic instances and their sample counts, message layouts, metadata, initial and
default parameters, text messages, and logging dropouts. A C++ library exposes typed samples,
incremental parsing, and log writing.

## Requirements

- A C++17 compiler, CMake 3.16 or newer, and a little-endian system.
- Optional: an installed doctest CMake package for the C++ unit tests.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/install"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build
```

The CLI, library, and default CLI tests build without external dependencies. Set
`-DFLIGHT_LOG_ENABLE_UNIT_TESTS=ON` to include the C++ test suite and
`-DFLIGHT_LOG_BUILD_EXAMPLES=ON` to build the reader and writer examples. Set
`-DBUILD_TESTING=OFF` to skip test registration.

## Quick start

```sh
./install/bin/flight-log-inspector tests/log_files/sample_log_small.ulg
```

In the topic listing, `sensor_combined (0)   -  1298` means instance 0 has 1,298 samples.
Parsing warnings go to standard error. A fatal parse error or an unreadable file produces a
nonzero exit status. The full report keeps samples in memory while it scans the log. The C++
reader can process chunks incrementally for large logs. Call `Reader::finish()` after the last
chunk to report incomplete input. Logs with appended data segments
may yield partial results.

Add `--json` before the path to produce one machine-readable diagnostics report.
Schema version 1 reports `status`, parsing `diagnostics`, `dropouts` with a
count and total duration in milliseconds, and `topics` with each name, instance
and sample count. Status is `ok` for a log accepted without diagnostics,
`warning` for recoverable parsing problems, and `error` for an unreadable log
or fatal parsing failure. Both successful statuses exit with code 0. Errors
still produce a JSON report and exit with code 1.

## Compare initial parameters

```sh
./install/bin/flight-log-inspector diff-params before.ulg after.ulg
```

Use `diff-params` to compare the configuration recorded at the start of each flight. It lists
added and removed parameters, value changes, and type changes in parameter-name order. It
compares values as stored and prints floating-point values with round-trip precision. A
successful comparison exits with status 0 whether or not there are differences.

## Summary for large logs

```sh
./install/bin/flight-log-inspector --summary-only tests/log_files/sample.ulg
```

This mode scans the full log and prints each topic instance's sample count and the dropout totals.
It keeps one sample counter per observed subscription and two counters for dropouts. Memory use
also includes message formats, topic definitions, header metadata, parameter defaults, and
parsing diagnostics.

## C++ library

Point `CMAKE_PREFIX_PATH` to the installation directory, then link the library in your project.

```cmake
find_package(flight_log CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE flight_log::flight_log)
```

Public headers are under `flight_log/`. The [examples](examples/) show typed sample access,
streaming reads, and file writing.

## Export topic samples

```sh
./install/bin/flight-log-inspector export-topic tests/log_files/sample_log_small.ulg sensor_combined \
  --start-us 20488019 --end-us 20492907 > sensor_combined.csv
```

The command writes CSV to standard output in recorded sample order. The bounds are inclusive
and use raw log timestamps in microseconds. Omit either bound to leave that end open. Use
`--instance N` to select a topic instance. Instance 0 is the default. The CSV header lists
fields in schema order, with indexed columns for arrays, dotted names for nested fields, and
one text column for each character array. Padding fields are omitted. A window without samples
still writes the header.

## Contributing and license

See [CONTRIBUTING.md](CONTRIBUTING.md). The project is distributed under the BSD 3-Clause
License in [LICENSE](LICENSE). Source and fixture attribution are recorded in [NOTICE](NOTICE).
