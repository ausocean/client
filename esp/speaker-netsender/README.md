# Speaker Netsender

This project is for an ethernet connected ESP32 controller which implements the netsender protocol. This controller is PoE powered, has a removable SD card, and has an onboard 20W amplifier capable of driving a 8Ω speaker.

## Formatting

ESP-IDF provides the following formatter command for formatting C and C++ files:

```bash
$ astyle --style=otbs --attach-namespaces --attach-classes --indent=spaces=4 --convert-tabs --align-reference=name --keep-one-line-statements --pad-header --pad-oper --unpad-paren --max-continuation-indent=120 -r -n "*.hpp,*.cpp,*.c,*.h"
```

## Managing Includes

`scripts/run_iwyu.sh` uses [Include What You Use (IWYU)](https://include-what-you-use.org/) to
identify missing or unnecessary `#include` directives in the project's source files.

This should be run:

- After adding new functionality, to check whether any new includes are needed
- During code review, to catch unnecessary includes that increase compile times
- When refactoring, to confirm that includes are still required after removing code

To run it, first Include What You Need must be installed on your system. Then IWYU can be used to manage a single file, or a whole directory.

```bash
# Analyze a single file
./scripts/run_iwyu.sh main/main.cpp

# Format a directory
./scripts/run_iwyu main/
```

**Keeping includes that IWYU wants to remove:**

IWYU sometimes suggests removing ESP-IDF driver headers that are actually needed
as direct includes, because it can see them transitively through other headers.
Those transitive paths are implementation details that can change between IDF
versions, so it is safer to include driver headers explicitly. To tell IWYU to
leave a specific include alone, add a pragma comment:

```cpp
#include "driver/i2c_master.h"  // IWYU pragma: keep
#include "driver/i2s_common.h"  // IWYU pragma: keep
#include "driver/i2s_std.h"     // IWYU pragma: keep
```
