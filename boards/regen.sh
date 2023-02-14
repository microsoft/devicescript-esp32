#!/bin/sh

set -e
set -x
npx typescript-json-schema --noExtraProps --required --defaultNumberType integer \
		esp32.d.ts ESP32DeviceConfig --out esp32deviceconfig.schema.json
npx typescript-json-schema --noExtraProps --required --defaultNumberType integer \
		esp32.d.ts ESP32ArchConfig --out esp32archconfig.schema.json
