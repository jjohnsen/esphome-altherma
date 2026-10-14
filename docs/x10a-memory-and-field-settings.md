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
| Normal register | length, \`40\`, register, checksum | \`40\`, register, length, data, checksum | Already implemented |
| Software/firmware ID | \`02 4C B1\` | \`4C\` + 4 bytes + checksum | ESPAltherma beta \`control.h\` |
| Read 16 RAM bytes | \`04 52\` + 2-byte LE address + checksum | \`52\` + 16 bytes + checksum | ESPAltherma beta \`control.h\` |
| Read one configuration-memory byte | \`04 49\` + 2-byte LE offset + checksum | \`49\` + 1 byte + checksum | ESPAltherma beta \`control.h\` |
| Read field setting by page/code | \`08 21 49 00 01 01\` + page + setting + checksum | **Unknown/untested**; log raw | Historical ESPAltherma protocol notes only |

The last two commands both contain byte \`49\` but **are not equivalent**.
A direct memory read uses command \`0x49\`, whereas the field-setting hypothesis
uses main command \`0x21\` and sub-command \`0x49\`.

## Enable explicitly

Add the following under your existing \`altherma_hub:\` block
(when compiling **this branch's component**, not \`@main\`):

\`\`\`yaml
altherma_hub:
  id: altherma_hub_1
  uart_id: uart_bus
  query_result_text_sensor: query_registry_result
  enable_memory_diagnostics: true
  enable_experimental_field_settings: true  # Optional, unverified command family
\`\`\`

The \`enable_memory_diagnostics\` flag gates \`read_memory\` and
\`read_software_id\`. The second flag is **separately required** for
\`read_field_setting\`; it is off by default. Ordinary polling and
\`query_register\` continue to work without either flag.

### Home Assistant service actions

All requests are queued and executed by the hub's existing non-blocking UART
state machine. One diagnostic request at a time; diagnostics wait until an
existing poll or manual register query completes. Responses go to ESPHome logs
and the existing **Manual Query Registry Result** diagnostic text sensor.

Read a known 16-byte RAM window (the command returns 16 bytes regardless of
the requested length):

\`\`\`yaml
action: esphome.esphome_altherma_read_memory
data:
  space: ram
  address: "0x187A"
  length: 16
\`\`\`

Read 16 consecutive configuration-memory bytes (16 distinct X10A requests):

\`\`\`yaml
action: esphome.esphome_altherma_read_memory
data:
  space: memory
  address: "0x016E"
  length: 16
\`\`\`

Read the hydro-board software ID before interpreting physical addresses:

\`\`\`yaml
action: esphome.esphome_altherma_read_software_id
\`\`\`

Experimental field-setting request (the response format is **unconfirmed**):

\`\`\`yaml
action: esphome.esphome_altherma_read_field_setting
data:
  code: "[1-02]"
\`\`\`

**Limits:** address is a string (\`0x\` hexadecimal or decimal);
length is 1-64 bytes per service call. RAM is collected in complete
16-byte windows; configuration memory is read byte-by-byte.
The response contains a HEX byte dump. There is no automatic address scanning.
The entity name and service prefix depend on the ESPHome node name.

## Field-setting candidates from the Daikin manual

**Heat pump family:** ERGA04-08DAV3(A) + EHVH04/08S18/23DA6V.
The installer's **Field settings table** identifies these settings:

| Field code | Meaning | ESPAltherma beta example RAM | Beta memory offset |
| --- | --- | --- | --- |
| \`[0-0B]\` | DHW weather curve, high outdoor temperature | \`0x187A\` | \`366\` (\`0x016E\`) |
| \`[0-0C]\` | DHW weather curve, low outdoor temperature | \`0x18B8\` | \`367\` (\`0x016F\`) |
| \`[1-02]\` | Main-zone leaving water, low outdoor temperature | \`0x18BA\` | \`372\` (\`0x0174\`) |
| \`[1-03]\` | Main-zone leaving water, high outdoor temperature | \`0x187C\` | \`373\` (\`0x0175\`) |

**The last two columns are NOT established for EHVH08S23DA6V.**
They come from the single \`CONTROL_PROFILES\` record keyed by software ID
\`18002904\`, reported on **EAVH16S18DA6V**. The logical mapping is a research
hypothesis that must be checked against each controller's real readbacks.
In that beta profile, RAM temperatures are signed fixed-point values
(temperature × 256); saved bytes encode \`(degrees - minimum) << 2\` with two
low flag bits. **Do not use the encoding to write anything on another controller.**

### Reading a field setting: expected versus implemented behavior

The historical \`Daikin I protocol.md\` contains a candidate command sequence
using *page* and *setting number* (for example page 5, setting 05). Its precise
reply length, byte layout, applicable software versions and whether it works on
Altherma 3 are **not documented**. This branch sends exactly the documented
read frame (never the write variant) and returns **raw HEX / NACK / timeout**.
It **does not claim to decode the actual setting value** until hardware tests
confirm the reply format. If it fails, use installer UI and read-only memory
diffs instead.

## Suggested experimental workflow

1. Query \`read_software_id\`; record the model and returned ID.
2. Read small candidate memory regions at a steady operating point; save
   log output with timestamps.
3. Change **one** low-risk field setting from the manufacturer's own UI,
   then read the *same* regions again.
4. Repeat the measurement, ideally reverting the change from the UI, and
   correlate consistent byte differences with the manual's value.
5. Document software ID, code, address, original value, and repeatability.
   **Never infer a general-purpose memory map from a single unit.**

RAM changes dynamically as the heat pump runs, so two dumps are not perfectly
synchronized snapshots. Reject NACK \`15 EA\` replies and CRC errors; do not
interpret these as values. The experimental field-setting subcommand may fail
even when ordinary sensor reading works.

## Primary sources and research trail

- [Daikin Installer reference guide 4P618956-1, ERGA04-08DAV3 + EHVH-UD6V](https://www.daikin.eu/content/dam/document-library/Installer-reference-guide/heat/air-to-water-heat-pump-low-temperature/EHVH-UD6V%2CERGA04-08DVA_4PEN618956-1_Installer%20reference%20guide_English.pdf) — field codes, descriptions, range/defaults (see "Field settings table").
- [ESPAltherma beta \`include/control.h\`](https://github.com/raomin/ESPAltherma/blob/beta/include/control.h) — observed \`0x4C\`, \`0x52\`, \`0x49\` read protocol and one firmware-specific address profile.
- [ESPAltherma \`Daikin I protocol.md\`](https://github.com/raomin/ESPAltherma/blob/beta/doc/Daikin%20I%20protocol.md) — \`0x40\` register protocol, checksum, and the **unverified** \`0x21/0x49\` page/setting read example.
- [ESPAltherma issue #1: control over serial port](https://github.com/raomin/ESPAltherma/issues/1) — community reverse-engineering of EEPROM reads and comparisons with physical EEPROM dumps.
- [ESPAltherma issue #40: X10A used by a bi-zone board](https://github.com/raomin/ESPAltherma/issues/40) — captures of another X10A command family, underscoring model-specific differences.
- [pyTherma](https://github.com/qris/pytherma) — third-party reconstruction of Daikin monitoring traffic and D-Checker diagnostics.
- [Daikin Configurator v2.8.0 reference in issue #1](https://github.com/raomin/ESPAltherma/issues/1#issuecomment-767037091) — archive mentioned; no current binary protocol verification in this feature.

## Unit tests

The protocol helpers intentionally do not depend on ESPHome, so the frame,
checksum, NACK, address-boundary and field-code tests can run locally:

\`\`\`sh
g++ -std=c++11 -Wall -Wextra -Werror -pedantic \
  tests/test_x10a_read_protocol.cpp -o /tmp/test_x10a_read_protocol
/tmp/test_x10a_read_protocol
\`\`\`

Full ESPHome compilation and hardware tests must still be performed before
enabling experimental diagnostics on a real heat pump.
