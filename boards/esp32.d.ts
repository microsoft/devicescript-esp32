import { JsonComment, Pin } from "@devicescript/srvcfg"
import {
    DeviceConfig,
    ArchConfig,
} from "../devicescript/compiler/src/archconfig"

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
