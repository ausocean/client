# Speaker Netsender

This project is for an ethernet connected ESP32 controller which implements the netsender protocol. This controller is PoE powered, has a removable SD card, and has an onboard 20W amplifier capable of driving a 8Ω speaker.

## Formatting

ESP-IDF provides the following formatter command for formatting C and C++ files:

```bash
$ astyle --style=otbs --attach-namespaces --attach-classes --indent=spaces=4 --convert-tabs --align-reference=name --keep-one-line-statements --pad-header --pad-oper --unpad-paren --max-continuation-indent=120 -r -n "*.hpp,*.cpp,*.c,*.h"
```
