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

**Keeping includes that clang-tidy wants to remove:**

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
