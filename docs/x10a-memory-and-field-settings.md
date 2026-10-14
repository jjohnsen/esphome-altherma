# X10A read-only memory and field-setting discovery (experimental)

> **Safety and scope:** For developers, not a production configuration interface.
> All commands in this branch are reads. **No RAM/EEPROM/setting write API exists here.**
> A read command against an undocumented memory-mapped address or an unknown
> firmware version is not guaranteed harmless; prefer small, known ranges.
> Never enable experimental commands on a critical installation without understanding the risks.

## Why two different kinds of "settings"?

The Daikin installer guide uses **field codes** such as **[0-0B]** and **[1-02]**.
They are **logical identifiers, NOT RAM addresses or EEPROM offsets**.
The same logical setting may map to a different physical location with a
different hydro-board software version. Do not use values from another unit as
write addresses.

The X10A protocol has at least two distinct read paths:

| Operation | Request | Expected response | Confidence |
| --- | --- | --- | --- |
| Normal register | length, `40`, register, checksum | `40`, register, length, data, checksum | Already implemented |
| Software/firmware ID | `02 4C B1` | `4C` + 4 bytes + checksum | ESPAltherma beta `control.h` |
| Read 16 RAM bytes | `04 52` + 2-byte LE address + checksum | `52` + 16 bytes + checksum | ESPAltherma beta `control.h` |
| Read one configuration-memory byte | `04 49` + 2-byte LE offset + checksum | `49` + 1 byte + checksum | ESPAltherma beta `control.h` |
| Read field setting by page/code | `08 21 49 00 01 01` + page + setting + checksum | **Unknown/untested**; log raw | Historical ESPAltherma protocol notes only |

The last two commands both contain byte `49` but **are not equivalent**.
A direct memory read uses command `0x49`, whereas the field-setting hypothesis
uses main command `0x21` and sub-command `0x49`.

## Enable explicitly

Add the following under your existing `altherma_hub:` block
(when compiling **this branch's component**, not `@main`):

```yaml
altherma_hub:
  id: altherma_hub_1
  uart_id: uart_bus
  query_result_text_sensor: query_registry_result
  enable_memory_diagnostics: true
  enable_experimental_field_settings: true  # Optional, unverified command family
```

The `enable_memory_diagnostics` flag gates `read_memory` and
`read_software_id`. The second flag is **separately required** for
`read_field_setting`; it is off by default. Ordinary polling and
`query_register` continue to work without either flag.

### Home Assistant service actions

All requests are queued and executed by the hub's existing non-blocking UART
state machine. One diagnostic request at a time; diagnostics wait until an
existing poll or manual register query completes. Responses go to ESPHome logs
and the existing **Manual Query Registry Result** diagnostic text sensor.

Read a known 16-byte RAM window (the command returns 16 bytes regardless of
the requested length):

```yaml
action: esphome.esphome_altherma_read_memory
data:
  space: ram
  address: "0x187A"
  length: 16
```

Read 16 consecutive configuration-memory bytes (16 distinct X10A requests):

```yaml
action: esphome.esphome_altherma_read_memory
data:
  space: memory
  address: "0x016E"
  length: 16
```

Read the hydro-board software ID before interpreting physical addresses:

```yaml
action: esphome.esphome_altherma_read_software_id
```

Experimental field-setting request (the response format is **unconfirmed**):

```yaml
action: esphome.esphome_altherma_read_field_setting
data:
  code: "[1-02]"
```

**Limits:** address is a string (`0x` hexadecimal or decimal);
length is 1-64 bytes per service call. RAM is collected in complete
16-byte windows; configuration memory is read byte-by-byte.
The response contains a HEX byte dump. There is no automatic address scanning.
The entity name and service prefix depend on the ESPHome node name.

## Field-setting candidates from the Daikin manual

**Heat pump family:** ERGA04-08DAV3(A) + EHVH04/08S18/23DA6V(G).
The Daikin guide explicitly includes **EHVH08S23DA6V(G)**.
The installer's **Field settings table** identifies these settings:

| Field code | Meaning | ESPAltherma beta example RAM | Beta memory offset |
| --- | --- | --- | --- |
| `[0-0B]` | DHW weather curve, high outdoor temperature | `0x187A` | `366` (`0x016E`) |
| `[0-0C]` | DHW weather curve, low outdoor temperature | `0x18B8` | `367` (`0x016F`) |
| `[1-02]` | Main-zone leaving water, low outdoor temperature | `0x18BA` | `372` (`0x0174`) |
| `[1-03]` | Main-zone leaving water, high outdoor temperature | `0x187C` | `373` (`0x0175`) |

**The last two columns are NOT established for EHVH08S23DA6V.**
They come from the single `CONTROL_PROFILES` record keyed by software ID
`18002904`, reported on **EAVH16S18DA6V**. The logical mapping is a research
hypothesis that must be checked against each controller's real readbacks.
In that beta profile, RAM temperatures are signed fixed-point values
(temperature × 256); saved bytes encode `(degrees - minimum) << 2` with two
low flag bits. **Do not use the encoding to write anything on another controller.**

### Cross-check: EAVH16S18DA6V (upstream control example)

Daikin's **Field settings table 4P556072-1A (2019.12)** explicitly includes
**EAVH16S18DA6V**. ESPAltherma beta's sole writable
`CONTROL_PROFILES` entry is for hydro-board software **18002904**,
reported on this model. Matching the documented two-point weather curves to
the upstream code gives:

| UI menu / field code | Meaning | Factory default | Allowed range in Daikin manual | Upstream RAM | Upstream memory offset |
| --- | --- | --- | --- | --- | --- |
| 5.C / `[0-0B]` | DHW target at warm outdoor end | 55°C | 35°C to `[6-0E]` | `0x187A` | 366 (`0x016E`) |
| 5.C / `[0-0C]` | DHW target at cold outdoor end | 60°C | 45°C to `[6-0E]` | `0x18B8` | 367 (`0x016F`) |
| 2.5 / `[1-02]` | Main-zone LWT at cold outdoor end | 35°C | `[9-01]` to `[9-00]` | `0x18BA` | 372 (`0x0174`) |
| 2.5 / `[1-03]` | Main-zone LWT at warm outdoor end | 25°C | `[9-01]` to min(45°C, `[9-00]`) | `0x187C` | 373 (`0x0175`) |

**Do not mistake this cross-reference for an independently verified memory map.**
Daikin documents the menu fields; ESPAltherma beta documents RAM/memory
positions for software 18002904. Their meaning agrees, but proving each
field-to-offset association requires a controlled, read-only comparison
against values displayed on the EAVH's UI.

**Why this matters for flat-curve control:** The factory-default
`[6-0E]` maximum tank temperature is **60°C for EAV models**, while
`[9-01]` (main-zone LWT minimum) defaults to **25°C** and `[9-00]`
(maximum) defaults to **55°C**. A flat curve requires **both endpoints**
to accept the same temperature, so using these *default* limits gives
**45–60°C for DHW** and **25–45°C for LWT**. The upstream implementation
reads model-specific limit records from RAM rather than assuming these
defaults; user configuration may differ.

Other related codes are **not** written by the upstream controller:
`[0-0D]` / `[0-0E]` (DHW outdoor-temperature axis),
`[1-00]` / `[1-01]` (LWT outdoor-temperature axis),
and `[6-0A]` / `[6-0B]` / `[6-0C]` (tank comfort/eco/reheat).
Its purpose is to make the existing **weather-dependent curve flat**,
not to change the UI's fixed-setpoint or scheduled modes directly.

**Setpoint modes have menu numbers but no field code** in the table:
`[5.B]` selects fixed (0) or weather-dependent DHW (1), and
`[2.4]` selects fixed (0), weather-dependent heating/fixed cooling (1),
or weather-dependent operation (2) for the main zone.
The upstream firmware profile checks dedicated memory bytes **278**
(mask `0x01`, DHW) and **374** (mask `0x0C`, LWT) to verify curve
mode. These are firmware-specific, not the hexadecimal translation
of the menu number.

With the upstream's encoded memory format `((temp - minimum) << 2) | flags`,
factory-default curve points would have these **upper six-bit patterns**:
`0x50` at 366, `0x3C` at 367, `0x28` at 372, `0x00` at 373,
**after masking each byte with `0xFC`**. These are *predictions* only:
real installations often change defaults, and each minimum must be confirmed
using the unit's limit records. Never write predicted values.

Sources: [Daikin's installer guide for EPGA11-16D + EAVH/X16S18+23D
(4P606487-1, December 2019)](https://www.daikin.eu/content/dam/document-library/Installer-reference-guide/heat/air-to-water-heat-pump-low-temperature/EPGA011-016DV%2CEAVH-D6V_4P606487-1_2019_12_Installer%20reference%20guide_English.pdf);
[Field settings table 4P556072-1A (10 pages)](https://www.scribd.com/document/455525373/EABH-X-D6V-9W-EAVH-X-D6V-9W-4PEN556072-1A-2019-12-Field-settings-table-English);
[ESPAltherma beta `control.h`](https://github.com/raomin/ESPAltherma/blob/beta/include/control.h).

### Reading a field setting: expected versus implemented behavior

The historical `Daikin I protocol.md` contains a candidate command sequence
using *page* and *setting number* (for example page 5, setting 05). Its precise
reply length, byte layout, applicable software versions and whether it works on
Altherma 3 are **not documented**. This branch sends exactly the documented
read frame (never the write variant) and returns **raw HEX / NACK / timeout**.
It **does not claim to decode the actual setting value** until hardware tests
confirm the reply format. If it fails, use installer UI and read-only memory
diffs instead.

## Suggested experimental workflow

1. Query `read_software_id`; record the model and returned ID.
2. Read small candidate memory regions at a steady operating point; save
   log output with timestamps.
3. Change **one** low-risk field setting from the manufacturer's own UI,
   then read the *same* regions again.
4. Repeat the measurement, ideally reverting the change from the UI, and
   correlate consistent byte differences with the manual's value.
5. Document software ID, code, address, original value, and repeatability.
   **Never infer a general-purpose memory map from a single unit.**

RAM changes dynamically as the heat pump runs, so two dumps are not perfectly
synchronized snapshots. Reject NACK `15 EA` replies and CRC errors; do not
interpret these as values. The experimental field-setting subcommand may fail
even when ordinary sensor reading works.

## Primary sources and research trail

- [Daikin Installer reference guide 4P495248-1A (2018.11), ERGA04-08DAV3(A) + EHVH08S23DA6V(G)](https://www.daikin.ee/content/dam/document-library/Installer-reference-guide/heat/air-to-water-heat-pump-low-temperature/ERGA-DVA%2C%20EHVH-D9WG%2C%20EHVH-D6VG%2C%20EHVX-D9WG%2C%20EHVH-D6VG_4PEN495248-1A_2018_11_installer%20reference%20guide_English.pdf) — exact family and model named in cover; field settings table (PDF pages 97-106) and weather-dependent curve explanations (PDF pages 54, 65).
- [Daikin installer guide 4P606487-1 (2019.12), EPGA11-16D + EAVH16S18DA6V](https://www.daikin.eu/content/dam/document-library/Installer-reference-guide/heat/air-to-water-heat-pump-low-temperature/EPGA011-016DV%2CEAVH-D6V_4P606487-1_2019_12_Installer%20reference%20guide_English.pdf) — exact model used for the upstream control example; field settings table 4P556072-1A.
- [Field settings table 4P556072-1A (2019.12)](https://www.scribd.com/document/455525373/EABH-X-D6V-9W-EAVH-X-D6V-9W-4PEN556072-1A-2019-12-Field-settings-table-English) — 10-page standalone field-code list explicitly naming EAVH16S18DA6V.
- [ESPAltherma beta `include/control.h`](https://github.com/raomin/ESPAltherma/blob/beta/include/control.h) — observed `0x4C`, `0x52`, `0x49` read protocol and one firmware-specific address profile.
- [ESPAltherma `Daikin I protocol.md`](https://github.com/raomin/ESPAltherma/blob/beta/doc/Daikin%20I%20protocol.md) — `0x40` register protocol, checksum, and the **unverified** `0x21/0x49` page/setting read example.
- [ESPAltherma issue #1: control over serial port](https://github.com/raomin/ESPAltherma/issues/1) — community reverse-engineering of EEPROM reads and comparisons with physical EEPROM dumps.
- [ESPAltherma issue #40: X10A used by a bi-zone board](https://github.com/raomin/ESPAltherma/issues/40) — captures of another X10A command family, underscoring model-specific differences.
- [pyTherma](https://github.com/qris/pytherma) — third-party reconstruction of Daikin monitoring traffic and D-Checker diagnostics.
- [Daikin Configurator v2.8.0 reference in issue #1](https://github.com/raomin/ESPAltherma/issues/1#issuecomment-767037091) — archive mentioned; no current binary protocol verification in this feature.

## Unit tests

The protocol helpers intentionally do not depend on ESPHome, so the frame,
checksum, NACK, address-boundary and field-code tests can run locally:

```sh
g++ -std=c++11 -Wall -Wextra -Werror -pedantic \
  tests/test_x10a_read_protocol.cpp -o /tmp/test_x10a_read_protocol
/tmp/test_x10a_read_protocol
```

Full ESPHome compilation and hardware tests must still be performed before
enabling experimental diagnostics on a real heat pump.
