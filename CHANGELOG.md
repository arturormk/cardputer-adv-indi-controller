# Changelog

## Unreleased

Changes introduced after `v1.0`.

### Added

- Added direct mount slew-rate selection with number keys `1` through `9`, while keeping `[` and
  `]` for previous/next slew rate changes.
- Added GNSS elevation synchronization to the mount when the receiver reports a valid 3D elevation
  and the mount exposes writable `GEOGRAPHIC_COORD.ELEV`. When GNSS elevation is unavailable, sync
  preserves the mount's existing elevation value.
- Added mount and GNSS elevation rows to the mount GPS comparison screen.
- Added a `monitor` helper script for opening the PlatformIO serial monitor at `115200` baud, with
  `PORT` support for non-default serial devices.
- Added `docs/ROADMAP.md` describing planned controller improvements for mount state controls,
  camera capture workflow, filter wheels, focusers, and broader capability modeling.

### Changed

- Removed the mount GPS sync screen title and shifted its comparison rows upward so the GPS UTC
  row no longer overlaps the bottom help text.
- Renamed release firmware assets from `firmware-<tag>-cardputer-adv.bin` to
  `indi-controller-<tag>.bin`.
- Bumped the firmware version metadata from `1.0` to `1.1.6`.
- Documented the parser/property-cache overflow warning indicator shown as a yellow `!`.

### Verified

- Native INDI/GNSS tests pass with `pio test -e native`.
- The `cardputer-adv` firmware target builds successfully with `pio run -e cardputer-adv`.

## v1.0 - 2026-06-14

Initial stable release of the Cardputer INDI Controller.

### Included

- On-device Wi-Fi selection and persistent INDI server configuration.
- Automatic INDI device and property discovery with a generic read-only property inspector.
- Telescope mount screen with connection/tracking status, tracking mode, slew speed, RA/Dec,
  Alt/Az display where available, hold-to-nudge motion, selectable slew rates, and emergency abort.
- Camera screen with standard `CCD_EXPOSURE` capture support and compatible writable ISO switch
  selection.
- Automatic Wi-Fi and INDI server reconnection.
- GNSS expansion-module detection from valid NMEA traffic.
- GNSS status screen showing UTC, latitude, longitude, 3D-fix elevation, satellite count, fix
  status, and HDOP.
- Mount GPS comparison and sync for GNSS latitude, longitude, and UTC on compatible mounts.
- INDI BLOB transfers disabled to avoid downloading image data to the Cardputer.
- GitHub Actions release workflow that builds and uploads a complete flashable firmware image for
  `v*` tags.
