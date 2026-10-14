# Experimental automatic identification (X10A)

This work builds on the optional read-only diagnostics in PR #50.
It is **not** automatic sensor-profile selection yet.

## Evidence and scope

Static inspection (without executing the program) of the supplied
**Daikin Configurator.exe** identifies `Cls_CommDriver.r_SendVcommand_forAutoDetect`
and `r_Comm_MVcom`. The former queues a `V` command; the latter frames it
as `02 56 A7`. The corresponding response handler
`r_Comm_MVsts` obtains the two-byte **Micon ID** from the second and
third response bytes, little-endian.

| Query | Request | Expected reply format | Observed on `17014001`? |
| --- | --- | --- | --- |
| Hydro software ID | `02 4C B1` | `4C <four data bytes> <CRC>` | **Yes**, `17014001` |
| Micon ID | `02 56 A7` | `56 <id low> <id high> <CRC>` | **No**, not yet tested |

For example, `56 62 38 0F` would decode to `ID3862`. This is
a **synthetic example** using an ID from an older CA-series
`appliance.nfo`; it is **not** a measurement from the user's DA unit.
These IDs must not be confused with `17014001`, a software ID.

No write, initialization, boot-check, controller reset or memory-access
command is sent by this feature. We do not attempt COM-port discovery,
which has no meaning on a fixed ESP32 UART.

## What this branch adds

- `read_micon_id`: explicit, one-shot experimental `0x56` query.
- `identify_controller`: sequential `0x4C` and `0x56` queries using
  the existing diagnostic UART scheduler (never interleaved with polling).
- Optional `software_id_text_sensor` and `micon_id_text_sensor`
  pointers for separate diagnostic Home Assistant entities. Results also
  appear in the existing `Manual Query Registry Result` and ESPHome logs.
- `identify_on_boot`, **off by default**. Enables the one-shot
  two-query sequence once during startup, only when
  `enable_memory_diagnostics: true`.
- Four-byte checksum/command validation and little-endian Micon parsing,
  tested in the host-side C++ unit test.

Everything remains behind the pre-existing optional flag
`enable_memory_diagnostics`; production defaults are unchanged.
The existing experimental field-setting `0x21/0x49` command is
unrelated and is *not* invoked.

## First manual test on M5Stack

While building firmware from branch
`feature/x10a-auto-identification`, configure in `base.yaml`:

```yaml
altherma_hub:
  id: altherma_hub_1
  uart_id: uart_bus
  update_interval: 30s
  query_result_text_sensor: query_registry_result
  enable_memory_diagnostics: true
  identify_on_boot: false
  software_id_text_sensor: altherma_hydro_software_id
  micon_id_text_sensor: altherma_hydro_micon_id

text_sensor:
  - platform: template
    name: "Manual Query Registry Result"
    id: query_registry_result
    entity_category: diagnostic
  - platform: template
    name: "Hydro Software ID"
    id: altherma_hydro_software_id
    entity_category: diagnostic
  - platform: template
    name: "Hydro Micon ID"
    id: altherma_hydro_micon_id
    entity_category: diagnostic
```

After compiling/flashing and reconnecting the ESPHome integration,
call **Developer Tools → Actions**:

```yaml
action: esphome.esphome_altherma_m5stack_read_micon_id
```

Expect either `X10A hydro Micon ID=IDXXXX`, an explicit NACK, a
checksum error, or a timeout. **Record the complete ESPHome log.**
Do not assume a Micon ID uniquely identifies a physical pump model.

Then optionally compare with the combined command:

```yaml
action: esphome.esphome_altherma_m5stack_identify_controller
```

This should first publish `X10A hydro software ID=17014001` on the
known test unit, and then attempt the Micon query. If `0x56` is
rejected, the first result remains valid; the combined query ends
without silently retrying. Never activate boot detection until the
one-shot probe succeeds on the target unit.

## Why profile selection is a separate stage

ESPHome YAML packages are resolved at **firmware build time**. The
controller cannot switch between model-specific YAML packages while
running. A future factory flow could record identity and suggest a
profile at **installation / Take Control time**. Another design could
compile multiple register definitions and conditionally activate the
appropriate runtime subset, but must avoid duplicate sensor names and
unsupported conversions. Neither behavior is included in this branch.

The known `17014001` software ID has been observed on an
`ERGA08DAV3 / EHVH08S23DA6V` installation. This is a single
known example, **not** a sufficient one-to-one mapping for all units.

## Safety / current limitations

- Auto-detection is **opt-in**, disabled in default firmware.
- `0x56` is inferred from the supplied older Configurator executable,
  *not yet verified on the current DA hardware*.
- Successful communication does not itself prove model compatibility.
- No blanket memory scan, setting writes, or automatic profile switch.
- If an unexpected response is received, log it and stop; do not
  brute-force commands or retry many firmware variants.
