# Jacdac for ESP32

This repo contains implementation of Jacdac protocol for ESP32.
It's currently set up for ESP32-S2, in particular for
[Jacdac ESP32 Brain](https://microsoft.github.io/jacdac-docs/devices/microsoft-research/jmbrainesp3248v03/).

## Building

Install ESP-IDF. Make sure `IDF_PATH` is set.
You can also install `ccache` to speed up builds.
You will need to run `export.sh` from the IDF folder - the Makefile will remind you.

To build run `make`.

To deploy run `make r`.

## TODO

* [x] report Wi-Fi RSSI from tsagg
* [ ] deal with Discrete from motion sensor
* [ ] user-accessible watchdog in Jacscript
* [x] restart on infinite loop (hw-watchdog)
* [ ] impl. watchdogs in tsagg + azureiot
* [ ] blink on upload

* [ ] save program in flash
* [ ] multiple Wi-Fi networks saved

* [x] add HF2 over USB Serial (CDC)
* [x] set "CLIENT" flag in announce

* [ ] disable self-reports coming from the wire
* [ ] don't forward `restricted` packets to the wire from USB or loopback
* [ ] only accept `restricted` packets from USB or loopback
* [ ] add `restricted` flag in frame flags

* [ ] re-enable wifi service - think about auto-connect?
* [ ] implement Azure IoT Hub connection and IoT Hub health service
* [ ] implement Jacscript Cloud service using IoT Hub
* [ ] add auto-upload function in Jacscript and precompile for common modules
* [ ] implement reset_in as hw-watchdog

## Contributing

This project welcomes contributions and suggestions.  Most contributions require you to agree to a
Contributor License Agreement (CLA) declaring that you have the right to, and actually do, grant us
the rights to use your contribution. For details, visit https://cla.opensource.microsoft.com.

When you submit a pull request, a CLA bot will automatically determine whether you need to provide
a CLA and decorate the PR appropriately (e.g., status check, comment). Simply follow the instructions
provided by the bot. You will only need to do this once across all repos using our CLA.

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).
For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or
contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.
