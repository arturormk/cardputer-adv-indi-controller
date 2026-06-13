# Cardputer INDI Controller

A handheld Wi-Fi controller for astronomical equipment exposed through an
[INDI](https://indilib.org/) server, built for the M5Stack Cardputer ADV.

The controller discovers INDI devices at runtime and provides dedicated screens for telescope
mounts and cameras, plus a generic read-only property inspector. It is designed for common field
operations without requiring a phone or laptop at the telescope.

## Features

- On-device Wi-Fi selection and persistent INDI server configuration.
- Automatic INDI device and property discovery.
- Generic device, property, and member inspector.
- Telescope status display:
  - Connection and tracking state.
  - Tracking mode and slew speed.
  - RA/Dec and Alt/Az coordinates when available.
- Telescope controls:
  - Hold-to-nudge motion in four directions.
  - Selectable slew rates.
  - Emergency motion abort.
- Camera controls:
  - Select exposure time and trigger capture through standard `CCD_EXPOSURE`.
  - Select ISO when the driver exposes a writable ISO switch property.
- Automatic Wi-Fi and INDI server reconnection.
- Battery percentage in the status bar.
- INDI BLOB transfers disabled to avoid downloading images to the Cardputer.

## Compatibility

The controller is capability-based. It discovers standard INDI properties instead of depending on
a specific telescope or camera model.

Mount control uses properties such as:

- `CONNECTION`
- `EQUATORIAL_EOD_COORD`
- `HORIZONTAL_COORD`
- `TELESCOPE_TRACK_STATE`
- `TELESCOPE_TRACK_MODE`
- `TELESCOPE_MOTION_NS`
- `TELESCOPE_MOTION_WE`
- `TELESCOPE_SLEW_RATE`
- `TELESCOPE_ABORT_MOTION`

Camera capture uses the standard `CCD_EXPOSURE` property. ISO control is available only when the
camera driver exposes a compatible writable ISO switch property. Vendor-specific DSLR actions,
shutter-preset properties, image previews, and image downloads are not supported.

Driver support does not guarantee that the physical device implements every advertised property.
The INDI driver and equipment configuration determine which controls are available and whether the
device honors them.

## Controls

The Cardputer's printed punctuation keys act as directional arrows:

| Key | Action |
| --- | --- |
| `;` | Up |
| `.` | Down |
| `,` | Left / back |
| `/` | Right / open |
| Enter | Open |
| Backspace | Back |
| `S` | Open settings from the device list |

### Mount Screen

| Key | Action |
| --- | --- |
| Hold arrow keys | Nudge north, south, west, or east |
| Release arrow key | Stop motion on that axis |
| `[` / `]` | Previous / next slew rate |
| `C` | Connect / disconnect |
| `A` | Emergency motion abort |
| Enter | Open property inspector |
| Backspace | Return to device list |

The `A` abort shortcut also works from other screens when the selected device is a detected mount.

### Camera Screen

| Key | Action |
| --- | --- |
| `[` / `]` | Previous / next exposure time |
| `-` / `=` | Previous / next discovered ISO value |
| Space | Trigger exposure |
| `C` | Connect / disconnect |
| Enter | Open property inspector |
| Backspace | Return to device list |

## Configuration

Wi-Fi credentials and the INDI server hostname/IP and port are configured on the Cardputer and
stored in ESP32 NVS:

1. Press `S` from the device list.
2. Select a scanned Wi-Fi network and enter its password.
3. Enter the INDI server hostname/IP and port.
4. Select `Test & save`.

Settings are saved only after the Wi-Fi and INDI server connection tests succeed. Passwords are
masked on screen and are not written to serial logs. `Clear saved settings` removes the stored
configuration.

Development defaults can optionally be provided by copying `include/config.example.h` to
`include/config.h`. These defaults are used only when no saved NVS configuration exists.
`include/config.h` is ignored by Git to prevent credentials from being committed.

## Build, Test, and Flash

Install [PlatformIO](https://platformio.org/), connect the Cardputer ADV over USB, and run:

```bash
./flash
```

The script builds the `cardputer-adv` environment and uploads it directly to the device.

To build or run the native protocol tests separately:

```bash
~/.platformio/penv/bin/pio run -e cardputer-adv
~/.platformio/penv/bin/pio test -e native
```

Serial diagnostics are available at `115200` baud.

## Design Notes

The firmware uses a streaming XML parser and bounded property cache suitable for the Cardputer
ADV's memory constraints. INDI definitions and updates are processed incrementally, and the
display is rendered through an off-screen canvas to avoid redraw flicker.

See [docs/BLUEPRINT.md](docs/BLUEPRINT.md) for the original architecture and design rationale.

## License

This project is licensed under the [MIT License](LICENSE).
