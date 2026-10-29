# Speaker Netsender

This project is for an ethernet connected ESP32 controller which implements the netsender protocol. This controller is PoE powered, has a removable SD card, and has an onboard 20W amplifier capable of driving a 8Ω speaker.

## ESP-IDF setup

Install ESP-IDF v6.0 and follow the documentation on setting up your development environment. The project uses the clang toolchain, to do this add the follow environment variable to your environment:

```sh
export IDF_TOOLCHAIN=clang
```

## Formatting

There is an included `.clang-format` file which defines the formatting parameters for all c++ code and headers. Many IDEs support clangd intergration which will often read this file and apply the appropriate styles automatically. This may need to be configured, see your IDEs documentation for steps on how to do this.

Alternatively, clang-format can be run manually via the command line, using the following command for each file:

```sh
clang-format -i <filepath>
```

## Managing Includes

There is also a `.clang-tidy` file which specifically handles management of imports. This should also be set to run through clangd.

Alternatively this can also be run manually via the command line:

```sh
clang-tidy --fix <filepath>
```

## Speaker behaviour

### Volume

The `Volume` variable is interpreted as a value from 0 to 100. It is mapped onto
the TAS5805 digital volume using a square-law amplitude curve
(`40 * log10(vol / 100)` dB): 100 is 0 dB, 50 is about -12 dB, and 0 mutes the
output. This keeps lower settings audible and avoids the large jump at the top
of the range that a linear register mapping produces. The amplifier's positive
gain range is intentionally not used, so the maximum setting is 0 dB rather
than +24 dB.

### Audio downloads

Audio files are downloaded to a temporary `<sha256>.wav.tmp` file and only
renamed to `<sha256>.wav` after the HTTP request succeeds, the complete response
has been written, and the length matches the server's `Content-Length` where
provided. Cached files are checked for a valid WAV header before being reused,
so an interrupted download cannot leave a corrupt file that later looks like a
cache hit.

### SD card SPI clock

`CONFIG_SD_SPI_FREQ_KHZ` (under the `SD Card` menu) sets the SPI clock used to
access the SD card, and defaults to 10 MHz. Lowering this value can improve
reliability on boards that report intermittent `data CRC failed` or
`sdmmc_read_sectors_dma` errors during long playback sessions (issue #121).

## NetSender component notes

HTTP response parsing operates directly on the fixed response buffer and does
not allocate temporary strings. Pin readers and the variable parser are plain,
non-capturing function pointers, so callbacks must be ordinary functions rather
than lambdas with captures.

## Running the Pipi host tests

The `pipi` logging component has host tests that build and run natively on your Linux machine, so no ESP32 hardware is required. They live in `components/pipi/host_tests/test_pipi` and are a gtest-based ESP-IDF project that targets the `linux` host.

In addition to the ESP-IDF setup described above, your ESP-IDF installation must ship the clang Linux toolchain (`tools/cmake/toolchain-clang-linux.cmake`), as the tests are built with the clang toolchain. The `clang`, `clang++` and `lld` binaries also need to be installed on your system.

To build and run the tests:

```sh
cd components/pipi/host_tests/test_pipi
idf.py build
./build/test_pipi.elf
```

The test target is already pinned to `linux` in the committed `sdkconfig`, so no `set-target` step is required. The project's `CMakeLists.txt` enables the local `gtest` component (defining `RUNNING_UNIT_TESTS`) automatically. Run the tests from the `test_pipi` directory, as the suite creates and `chdir`s into a `test/` subdirectory to isolate its log files. Individual tests can be selected with gtest flags, e.g.:

```sh
./build/test_pipi.elf --gtest_filter=TestEntry.*
```

Note that some ESP-IDF point releases do not include `toolchain-clang-linux.cmake` (e.g. v6.0.2), which will cause the build to fail with a "toolchain file not found" error. If that happens, use an ESP-IDF version that provides it.

## Keeping includes that clang-tidy wants to remove:

clang-tidy sometimes suggests removing ESP-IDF driver headers that are actually needed
as direct includes, because it can see them transitively through other headers.
Those transitive paths are implementation details that can change between IDF
versions, so it is safer to include driver headers explicitly. To tell clang-tidy to
leave a specific include alone, add a pragma comment:

```cpp
#include "driver/i2c_master.h"  // IWYU pragma: keep
#include "driver/i2s_common.h"  // IWYU pragma: keep
#include "driver/i2s_std.h"     // IWYU pragma: keep
```
