/// <reference path="../devicescript/runtime/jacdac-c/dcfg/srvcfg.d.ts" />

import {
    ArchConfig,
    DeviceConfig,
    JsonComment,
    Pin,
} from "@devicescript/srvcfg"

interface ESP32DeviceConfig extends DeviceConfig {
    sd?: SdCardConfig
}

interface ESP32ArchConfig extends ArchConfig {}

interface SdCardConfig extends JsonComment {
    pinMISO: Pin
    pinMOSI: Pin
    pinSCK: Pin
    pinCS: Pin
}
