# Experimental dynamic sensor registration (ESP32)

**Scope:** An isolated proof of concept on branch `feature/dynamic-sensors-mock`.
It does not modify the default configurations or change X10A polling.

## Goal

Verify that native ESPHome sensors can be created in C++ during
`AlthermaHub::setup()`, instead of defining each sensor in YAML.

The component uses `App.register_sensor(sensor, name, 0, 0)`.
The zero hash makes ESPHome compute a stable object ID from the
literal entity name. Each dynamically created `sensor::Sensor`
has its own pointer and persistent (static-literal) name.

This tests **registration at boot**, not adding or removing entities after
the ESPHome API has started and not automatic Daikin model detection.

## Run

On an ESP32 DevKit, from the repository root:

```sh
python -m pip install esphome
esphome config esphome-altherma-dynamic-mock.yaml
esphome run esphome-altherma-dynamic-mock.yaml
```

The configuration uses an ESP32, native API, fallback Wi-Fi AP and captive
portal. Provision Wi-Fi through the fallback AP if the board does not have
credentials. You do **not** need a connected heat pump or X10A bus.

## Expected result

Look for this message at startup:

```text
Registered 3 mock ESPHome sensors dynamically at startup
```

When connected through the native ESPHome API, Home Assistant should
discover the following three entities:

| Native entity name | Initial value | Next update (10 s) |
| --- | --- | --- |
| Mock Outdoor Temperature | 7.0 | 7.1 |
| Mock Leaving Water Temperature | 32.0 | 32.2 |
| Mock Compressor Frequency | 30 | 31 |

Values are intentionally synthetic and have no physical units in this
proof of concept. They repeat every 10 updates. No X10A registers are
queried (the hub has no configured real sensors).

The mock entities are **not** configured in YAML and are **not** registered
in `AlthermaHub::sensors_`. They are native ESPHome API entities only.

## How it works

- Python component schema accepts `dynamic_mock_sensors: true`.
- During `AlthermaHub::setup()`, C++ constructs three `sensor::Sensor`
  instances and calls `App.register_sensor()` on each.
- During `update()`, the hub publishes synthetic values every 10 seconds.
- Default `dynamic_mock_sensors: false` preserves existing installations.

## Validation checklist

- [ ] `esphome config` passes.
- [ ] ESP32 firmware compiles.
- [ ] Boot log shows registration of exactly three mock entities.
- [ ] Home Assistant discovers all three via the ESPHome API.
- [ ] Each sensor updates every 10 seconds.
- [ ] Reboot retains stable entity identities without duplicates.
- [ ] Existing normal Altherma configuration behaves unchanged.

## Known limitations

- Not tested on physical hardware or in Home Assistant.
- `App.register_sensor()` is a low-level ESPHome API, not a published
  runtime entity factory contract; verify each ESPHome version.
- This intentionally omits device class and unit metadata. Newer ESPHome
  versions encode those as generated string-table indices.
- Entity registration happens during boot `setup()`. Discovery after
  connection would require additional API lifecycle work.
- Dynamic model detection and model-specific sensor profiles are future
  stages, not part of this proof of concept.
