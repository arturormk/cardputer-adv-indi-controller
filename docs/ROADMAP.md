# Future Feature Roadmap

## Purpose

The Cardputer INDI Controller already fulfills its original goal: it is a small, field-friendly
hand controller for common mount and camera operations. Future work should deepen that role without
turning the application into a reduced desktop astronomy suite.

This roadmap describes five features that fit the existing capability-driven INDI architecture:

1. Mount park/unpark and tracking controls.
2. Red night mode and display brightness control.
3. Basic focuser support.
4. Basic filter-wheel support.
5. Camera gain control.

These are candidates for future development, not commitments to a particular release schedule.
The current `v1.0` release remains complete and usable without them.

## Guiding Principles

- Remain generic and discover capabilities from INDI properties.
- Use standard INDI properties first and tolerate missing or driver-specific capabilities.
- Never send a command through a read-only property.
- Prefer actual property member names and labels reported by the driver.
- Keep screens and shortcuts simple enough to use outdoors.
- Preserve the bounded-memory design and continue rejecting image BLOBs.
- Treat potentially disruptive movement commands conservatively.
- Add focused native tests for detection, selection, command serialization, and edge cases.

## Recommended Sequence

The suggested implementation order balances usefulness, risk, and shared infrastructure:

1. Red night mode and brightness control.
2. Mount park/unpark and tracking controls.
3. Camera gain control.
4. Basic filter-wheel support.
5. Basic focuser support.

Night mode is self-contained and improves every observing session. Mount controls extend an
existing screen and use established switch-command behavior. Gain and filter-wheel support build
on the current camera interaction patterns. Focuser support is last because movement and stop
behavior require the most careful safety handling.

Each feature should be independently releasable. There is no need to wait until all five are
complete before publishing a future stable version.

## Mount Park/Unpark and Tracking Controls

### Goal

Allow common mount state changes directly from the mount screen while making the current park and
tracking states easy to see.

### Capability Detection

- Detect park support through a writable `TELESCOPE_PARK` switch property.
- Detect tracking support through a writable `TELESCOPE_TRACK_STATE` switch property.
- Determine current state from the active switch member.
- Prefer recognized park/unpark and tracking on/off member names, but use driver-provided labels
  and avoid commands when the intended action cannot be identified confidently.

### User Experience

- Show park state alongside the existing connection and tracking information.
- Provide a direct tracking toggle when the capability is available.
- Provide park and unpark actions only when their corresponding state transition is meaningful.
- Require an explicit confirmation before sending a park command.
- Stop manual mount motion before parking, disconnecting, or leaving the mount screen.
- Show unavailable, requested, acknowledged, and failed states through the existing feedback area.

### Safety and Compatibility

- Do not offer park or tracking commands for missing, read-only, or ambiguous properties.
- Do not assume that every mount can unpark or change tracking state.
- Do not treat a successfully written command as proof that the mount completed the operation;
  reflect subsequent INDI property state updates.
- Keep the emergency abort shortcut available regardless of park/tracking support.

### Completion Criteria

- Standard simulator and real-driver park/tracking properties are detected.
- Park requires confirmation and cannot be triggered accidentally by a single key press.
- Tracking and park states update when the INDI server reports changes.
- Unsupported mounts retain the existing mount-screen behavior without empty or misleading controls.

## Red Night Mode and Brightness Control

### Goal

Reduce glare and preserve dark adaptation while retaining a readable, high-contrast interface.

### User Experience

- Add a normal theme and a red night theme.
- Add a small set of useful brightness levels rather than a continuous editor.
- Make theme and brightness available from Settings.
- Apply theme changes immediately across all screens, including status, warning, feedback, and
  selection colors.
- Persist the selected theme and brightness in NVS.

### Design Constraints

- Night mode should avoid bright white, blue, green, and cyan pixels.
- Red warning and selection shades must remain distinguishable without relying only on color.
- Connection, busy, warning, and alert states must remain understandable in both themes.
- Centralize theme colors instead of scattering conditional color choices through drawing code.
- Keep display brightness within a range that does not make the screen unusable in daylight or
  effectively invisible after a reset.

### Completion Criteria

- Every screen renders legibly in both themes.
- No normal-theme accent colors leak into night mode.
- Theme and brightness survive restart and saved-settings changes.
- Status meanings remain distinguishable on the physical Cardputer display.

## Camera Gain Control

### Goal

Allow bounded adjustment of camera gain when a driver exposes a recognizable writable number
property.

### Capability Detection

Detect gain conservatively, in this order:

1. A writable number property named `CCD_GAIN`.
2. A writable `GAIN` member inside `CCD_CONTROLS`.
3. A single writable number member whose name or label clearly identifies gain.

Do not invent a relationship between ISO and gain. Cameras may expose either or both controls.

### User Experience

- Show current gain, valid range, and step when available.
- Add previous/next gain controls to the camera screen without displacing exposure and capture
  controls.
- Clamp requested values to the member's reported minimum and maximum.
- Use the reported step when valid; otherwise use a conservative derived increment.
- Use the driver-provided label when detection is not based on the standard `CCD_GAIN` property.

### Safety and Compatibility

- Do not expose gain controls when detection is ambiguous.
- Preserve other members when the selected gain belongs to a multi-member number vector.
- Treat missing or unusable range metadata defensively.
- Report command failure and continue showing the server-reported value.

### Completion Criteria

- Standard `CCD_GAIN` and `CCD_CONTROLS.GAIN` layouts are supported.
- Read-only and ambiguous gain-like properties are not presented as controls.
- Values never exceed reported bounds.
- ISO behavior remains unchanged on cameras that expose ISO.

## Basic Filter-Wheel Support

### Goal

Allow selection of the active filter from a dedicated screen for INDI filter-wheel devices.

### Capability Detection

- Classify a device as a filter wheel when it exposes a usable `FILTER_SLOT` number property.
- Use filter-name properties when available to present meaningful labels.
- Fall back to numbered slots when names are unavailable, incomplete, or exceed bounded storage.
- Respect the slot minimum and maximum reported by the driver.

### User Experience

- Open a dedicated filter-wheel screen from the device list.
- Show connection state, current slot, selected filter name, and property state.
- Use simple previous/next controls to request another slot.
- Show movement/busy state until the driver reports completion.
- Retain access to the generic property inspector.

### Safety and Compatibility

- Do not send slot changes through read-only properties.
- Do not assume slot numbering begins at one.
- Do not claim a filter change completed until the server reports the new slot and a non-busy state.
- Handle missing or mismatched filter-name lists without blocking numbered slot control.

### Completion Criteria

- Devices with `FILTER_SLOT` are classified and shown with a dedicated screen.
- Named and unnamed filter wheels are both usable.
- Slot selection respects the driver's bounds.
- Other unknown devices continue opening in the generic inspector.

## Basic Focuser Support

### Goal

Provide safe, basic focus adjustment without attempting autofocus or image analysis.

### Capability Detection

- Classify devices using standard focuser-related number and switch properties.
- Prefer absolute-position control when a writable absolute position is available.
- Support relative movement when the driver exposes a writable relative position.
- Support manual inward/outward motion only when clear motion and stop properties are available.
- Use reported bounds and step values for numeric movement.

Exact driver layouts vary, so focuser detection should live in a dedicated model rather than in
screen or keyboard code.

### User Experience

- Open a dedicated focuser screen from the device list.
- Show connection state, current position, requested position or step, and movement state.
- Allow bounded previous/next adjustments for absolute or relative focus.
- For manual motion, move only while the corresponding key is held and send stop on release.
- Provide a prominent stop action whenever movement is possible.
- Retain access to the generic property inspector.

### Safety and Compatibility

- Always send stop when leaving the focuser screen, losing the INDI connection, or encountering an
  input-state reset during manual movement.
- Never move through missing, read-only, or ambiguous properties.
- Clamp absolute targets and relative requests to reported limits.
- Do not implement autofocus, backlash compensation, or temperature compensation unless a later
  roadmap explicitly adds them.

### Completion Criteria

- At least absolute-position and relative-position simulator or representative driver layouts work.
- Manual motion cannot remain active after key release, screen exit, or connection loss.
- Requested values remain within reported limits.
- Unsupported focusers remain accessible through the generic inspector.

## Shared Engineering Work

Several changes should be implemented once and reused across these features:

- Add focused capability models for filter wheels and focusers, following the existing mount and
  camera model pattern.
- Extend camera and mount models instead of embedding new property-detection rules in UI code.
- Reuse the existing INDI switch and number writers; extend them only when a command must preserve
  multiple vector members.
- Add reusable helpers for writable-property checks, bounded numeric stepping, pending-command
  feedback, and active switch-member detection.
- Review property-cache priority rules so critical park, focuser, and filter-wheel controls are not
  discarded first when the cache reaches capacity.
- Keep device classification deterministic when a device exposes capabilities from more than one
  category.

## Explicitly Out of Scope

These features should not expand into:

- Autofocus routines or image-based focus scoring.
- Filter sequences, capture plans, or imaging automation.
- Image downloads, previews, FITS parsing, or storage.
- Plate solving, guiding, polar alignment, or object catalogs.
- Vendor-specific hardware protocols outside INDI.
- A general-purpose editor for every writable INDI property.

The desired result remains a small dedicated controller that performs a limited set of common
observing actions reliably.
