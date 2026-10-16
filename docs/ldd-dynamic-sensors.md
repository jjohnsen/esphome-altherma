# Daikin LDD -> native ESPHome runtime sensors (proof of concept)

The goal is to derive ESPHome sensor **definitions** from a real Daikin
D-Checker LDD, then dynamically construct and register native ESPHome
entities when the ESP32 starts.

## What is dynamic?

- **Definition extraction:** offline on a development computer using
  `tools/ldd_decode.py` and `tools/ldd_to_header.py`.
- **Entity registration:** at ESPHome **boot time** using
  `App.register_sensor(entity, name, 0, 0)`, plus the existing
  `AlthermaHub::register_sensor(entity)` mapping and X10A decoder.
- **Not yet supported:** uploading/switching .ldd profiles without firmware
  rebuild; automatic model identification; dynamic entity addition to a
  running API connection. This is a mock feature, not complete LDD support.

## Where the data comes from

The file `tests/fixtures/ldd_lt_da_sample.csv` contains 14 **real rows**
from an earlier decoded Daikin D-Checker `LT_DA_04-08kW` LDD definition.
No original vendor binary is included.

The most useful LDD fields map directly:

| LDD field | Altherma field | Example |
| --- | --- | --- |
| `DataId` | X10A registry | `0x20` |
| `Offset` | Offset in registry payload | `0` |
| `Bytes` | Byte count | `2` |
| `ConvId` | Existing ESPAltherma converter | `105` |
| `LabelStr` | Native sensor name | `Outdoor air temp.(R1T)` |
| `Enabled` / `IsDisplay` | Filter candidate sensors | `True` |
| `FormatStr` | Accuracy decimals | `%.1f` |

The mock fixture intentionally includes unsupported rows to confirm filtering.
The generator currently supports **only numeric converter IDs 101, 105, 151,
152** and concrete one-byte registry IDs. LDD wildcard `0x3*`, string,
enum, bitfield and command fields are **skipped**, not guessed. A generated
definition does **not** establish that the physical heat pump actually
supports the field.

## Regenerate the profile

Run from the repository root:

```sh
# Directly from extracted rows:
python3 tools/ldd_to_header.py tests/fixtures/ldd_lt_da_sample.csv

# Or from an original D-Checker LDD on your development computer:
python3 tools/ldd_to_header.py /path/to/model.ldd

# Optional explicit two-step inspection before generating:
python3 tools/ldd_decode.py /path/to/model.ldd --csv model.csv
python3 tools/ldd_to_header.py model.csv
```

This writes `components/altherma_hub/ldd_generated_profile.h`.
Review the entries, IDs, byte counts, conversions and names before deploying
the generated header. An LDD may contain addresses that do not correspond to
a compatible X10A protocol variant.

## Mock firmware (no heat pump required)

```sh
python3 -m unittest discover -s tests -p 'test_ldd_to_header.py'
esphome config esphome-altherma-ldd-mock.yaml
esphome run esphome-altherma-ldd-mock.yaml
```

Enable the device in Home Assistant through the native ESPHome API.
Expect **11** entities with the prefix `LDD`, including:

- `LDD Outdoor air temp.(R1T)`
- `LDD Discharge pipe temp.`
- `LDD DHW setpoint`
- `LDD Leaving water temp. before BUH (R1T)`

They publish **synthetic values every 10 seconds**. No UART bytes are sent,
so this validates only the profile-to-entity discovery path, not protocol
decoding or sensor measurement. The mock leaves physical heat pumps alone.

## Live read-only experiment (DO NOT use without bench validation)

To test real readings on an explicitly supported heat pump, start from
`esphome-altherma-ldd-mock.yaml`, change
`ldd_mock_values: false`, and connect the X10A UART.
This uses the existing register polling, frame checks and
`Converter::convert()` decoder with generated sensor offsets.

Check register compatibility, sensor meanings, and timing first.
Do **not** enable unvalidated writes. No writing is implemented here.

The example deliberately omits `device_class` and `unit_of_measurement`
metadata. Recent ESPHome builds use generated string tables for those;
metadata mapping should be designed and tested separately before adding
full model profiles.

## Next architecture stage

For a no-reflash model switch, convert LDD files to a validated,
versioned compact profile, persist it in NVS/flash, then load and register
entities at boot. Model detection can choose the stored profile. The
LDD parser itself should remain off the ESP32.
