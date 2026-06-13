# BLUEPRINT.md

# Cardputer INDI Controller

## 1. Project Summary

This project is a **generic INDI hand controller** for the **M5Stack Cardputer ADV**.

The goal is to create a small, self-contained Wi-Fi controller that can connect directly to an INDI server, discover telescope mounts and cameras, and provide a practical graphical interface for common observing tasks.

The project is not intended to replace KStars/Ekos, N.I.N.A., PHD2, or a full imaging suite. It is intended to be a **field-friendly handheld controller** for basic telescope and camera operations.

Primary target device:

* M5Stack Cardputer ADV
* ESP32-S3 class MCU
* 240 × 135 LCD
* 56-key keyboard
* Wi-Fi
* microSD
* M5Cardputer / M5Unified / M5GFX software stack

Primary target server:

* INDI server running on a Raspberry Pi or similar machine
* Usually reachable over Wi-Fi on TCP port `7624`

The first usable version should connect to an INDI server, discover devices, identify telescope and camera devices, and allow the user to control a small set of common properties.

---

## 2. Intended Use Case

A user is outdoors with a telescope setup controlled by a Raspberry Pi running `indiserver`.

Instead of using a laptop or phone for every small operation, the user can use the Cardputer as a dedicated handheld controller.

Example operations:

* Connect to INDI server over Wi-Fi.
* See whether mount and camera are connected.
* Connect or disconnect INDI devices.
* See mount status.
* See current RA/Dec if available.
* Slew to manually entered RA/Dec coordinates.
* Abort mount motion.
* Park or unpark the mount if supported.
* Enable or disable tracking if supported.
* Set camera exposure time.
* Start camera exposure.
* Set camera gain/sensitivity if a recognizable property exists.
* See exposure state: idle, busy, ok, alert.
* Inspect raw INDI properties when needed.

The controller should remain usable with gloves, cold fingers, and poor lighting. The interface should be clean, high-contrast, and simple.

---

## 3. Project Philosophy

The project should be:

* **Generic**, not tied to a single telescope or camera model.
* **INDI-native**, talking directly to the INDI protocol rather than to a custom proxy API.
* **Small**, avoiding desktop-style abstractions.
* **Graphical but minimal**, not a serial-terminal menu and not a heavy GUI framework.
* **Memory disciplined**, because ESP32-S3 memory is limited and INDI uses XML.
* **Incremental**, with a useful mount/camera MVP before advanced features.
* **Inspectable**, so unknown INDI properties can still be viewed and possibly edited.

The project should not try to do everything.

Do not build:

* A full astrophotography suite.
* A FITS viewer.
* A plate solver.
* A guiding application.
* A replacement for Ekos.
* A general-purpose XML DOM parser.
* A large GUI widget framework.
* A full image-transfer pipeline in the first version.

---

## 4. Technical Context

INDI is a property-based protocol. Devices expose properties. Clients discover and update those properties.

The client generally does not need to know the vendor-specific serial or USB protocol for each telescope or camera. That device-specific logic lives in the INDI driver. The client talks to the INDI server using XML messages over TCP.

Important INDI concepts:

* Devices expose property vectors.
* Property vectors contain one or more members.
* Vectors have types:

  * Number
  * Switch
  * Text
  * Light
  * BLOB
* Properties have names, labels, groups, states, and permissions.
* The client receives definitions and updates.
* The client sends new values.

Common standard properties of interest:

* `CONNECTION`
* `EQUATORIAL_EOD_COORD`
* `ON_COORD_SET`
* `TELESCOPE_ABORT_MOTION`
* `TELESCOPE_PARK`
* `TELESCOPE_TRACK_STATE`
* `CCD_EXPOSURE`
* Possible gain/sensitivity properties:

  * `CCD_GAIN`
  * `GAIN`
  * `CCD_CONTROLS`
  * driver-specific number vectors

Because gain/sensitivity is less standardized than exposure, the first version should treat gain support as opportunistic.

---

## 5. Development Environment

Preferred initial development stack:

* PlatformIO
* Arduino framework
* M5Cardputer
* M5Unified
* M5GFX

A plausible initial `platformio.ini` may use a generic ESP32-S3 board definition if PlatformIO does not yet provide a dedicated Cardputer ADV board.

Example direction:

```ini
[env:cardputer-adv]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

monitor_speed = 115200
upload_speed = 1500000

build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1

lib_deps =
    m5stack/M5Unified
    m5stack/M5GFX
    m5stack/M5Cardputer
```

This is a starting point, not a final guarantee. The exact board configuration should be validated on the actual Cardputer ADV hardware.

---

## 6. Proposed Repository Structure

```text
/
  BLUEPRINT.md
  README.md
  platformio.ini
  src/
    main.cpp

    app/
      App.cpp
      App.h
      AppState.cpp
      AppState.h
      Config.cpp
      Config.h

    wifi/
      WifiManager.cpp
      WifiManager.h

    indi/
      IndiClient.cpp
      IndiClient.h
      IndiXmlTokenizer.cpp
      IndiXmlTokenizer.h
      IndiProtocol.cpp
      IndiProtocol.h
      IndiPropertyCache.cpp
      IndiPropertyCache.h
      IndiTypes.h
      IndiWriter.cpp
      IndiWriter.h

    ui/
      Ui.cpp
      Ui.h
      Screen.h
      Theme.h
      MenuScreen.cpp
      MenuScreen.h
      DeviceListScreen.cpp
      DeviceListScreen.h
      MountScreen.cpp
      MountScreen.h
      CameraScreen.cpp
      CameraScreen.h
      ExposureScreen.cpp
      ExposureScreen.h
      PropertyInspectorScreen.cpp
      PropertyInspectorScreen.h
      TextInputScreen.cpp
      TextInputScreen.h
      NumberInputScreen.cpp
      NumberInputScreen.h
      StatusBar.cpp
      StatusBar.h

    util/
      FixedString.h
      RingBuffer.h
      Logger.cpp
      Logger.h

  test/
    indi_parser/
      sample_messages.xml
      README.md
```

For a first implementation, not every file is required. The structure is meant to encourage separation between:

* Hardware/UI code
* Wi-Fi/TCP code
* INDI parsing
* INDI property model
* Application state

---

## 7. High-Level Architecture

```text
+---------------------------------------------------+
|                    UI Layer                       |
|  Screens, menus, status bar, value editors         |
+--------------------------+------------------------+
                           |
                           v
+---------------------------------------------------+
|                  App State Layer                  |
|  Current server, devices, selected mount/camera    |
+--------------------------+------------------------+
                           |
                           v
+---------------------------------------------------+
|                  INDI Client Layer                |
|  TCP connection, getProperties, enableBLOB, writes |
+--------------------------+------------------------+
                           |
                           v
+---------------------------------------------------+
|              Streaming XML / INDI Parser          |
|  Tokenizes XML stream and updates property cache   |
+--------------------------+------------------------+
                           |
                           v
+---------------------------------------------------+
|                 INDI Property Cache               |
|  Compact model of devices, properties, members     |
+---------------------------------------------------+
```

The UI should not parse XML.

The XML parser should not draw UI.

The property cache should be generic and reusable.

---

## 8. Important Memory Rule

Do **not** parse INDI XML as a complete XML document.

INDI is a long-lived TCP stream containing many top-level XML elements. The client should process incoming bytes incrementally.

Do not do this:

```cpp
readEverythingIntoHugeBuffer();
parseFullXmlDocument();
```

Instead:

```cpp
while (client.connected()) {
    int n = client.read(buffer, sizeof(buffer));
    for (int i = 0; i < n; ++i) {
        xmlTokenizer.feed(buffer[i]);
    }
}
```

The XML tokenizer must preserve state across reads.

TCP read boundaries are not meaningful. One read may contain half a tag, one full tag, several messages, or the end of one message plus the beginning of another.

The parser must tolerate all of these:

```xml
<defNumberVector device="CCD Simulator" name="CCD_EX
```

then later:

```xml
POSURE" state="Idle">
```

The tokenizer should emit events such as:

* Start element
* Attribute key/value
* Text
* End element

The INDI protocol layer should consume these events and update the property cache.

---

## 9. BLOB Policy

Version 1 should avoid image BLOBs.

On connection, the client should request properties and disable BLOB delivery if possible.

The project should send an appropriate INDI BLOB policy such as:

```xml
<enableBLOB>Never</enableBLOB>
```

or the correct form required by the server/device if more specificity is needed.

The parser should still be defensive:

* Recognize `oneBLOB`.
* Do not store BLOB contents.
* Skip BLOB text payload.
* Apply maximum text-size limits.
* Avoid base64 accumulation in RAM.

Full FITS/image support is explicitly out of scope for v1.

A later version may support Pi-side thumbnail generation through a separate helper service, but that would be an optional feature and not part of the generic INDI core.

---

## 10. INDI Parser Scope

The parser does not need to be a fully general XML parser.

It needs to handle enough XML for INDI:

* Start tags
* End tags
* Self-closing tags if present
* Attributes with quoted values
* Text content
* Basic XML escaping for text/attributes

It does not need:

* XPath
* XML namespaces
* DTDs
* Schema validation
* Processing instructions
* Comments
* Entity expansion beyond common XML escapes
* DOM tree construction

Supported INDI elements should include at least:

* `getProperties`
* `defNumberVector`
* `defSwitchVector`
* `defTextVector`
* `defLightVector`
* `defBLOBVector`
* `setNumberVector`
* `setSwitchVector`
* `setTextVector`
* `setLightVector`
* `setBLOBVector`
* `newNumberVector`
* `newSwitchVector`
* `newTextVector`
* `oneNumber`
* `oneSwitch`
* `oneText`
* `oneLight`
* `oneBLOB`
* `delProperty`
* `message`
* `enableBLOB`

The client only needs to generate a small subset:

* `getProperties`
* `enableBLOB`
* `newNumberVector`
* `newSwitchVector`
* `newTextVector`

---

## 11. Property Cache Design

The property cache should store a compact, bounded representation of discovered INDI devices and properties.

Suggested model:

```cpp
enum class IndiPropertyType {
    Number,
    Switch,
    Text,
    Light,
    Blob,
    Unknown
};

enum class IndiState {
    Idle,
    Ok,
    Busy,
    Alert,
    Unknown
};

enum class IndiPermission {
    ReadOnly,
    WriteOnly,
    ReadWrite,
    Unknown
};

struct IndiMember {
    char name[40];
    char label[40];

    // For numbers
    double numberValue;
    double minValue;
    double maxValue;
    double stepValue;

    // For text/switch/light
    char textValue[64];

    bool active;
};

struct IndiProperty {
    char device[40];
    char name[48];
    char label[48];
    char group[40];

    IndiPropertyType type;
    IndiState state;
    IndiPermission permission;

    IndiMember members[8];
    uint8_t memberCount;

    bool defined;
    bool writable;
};
```

These sizes are suggestions. Codex should tune them carefully.

The property cache should have explicit limits:

```cpp
constexpr int MAX_DEVICES = 8;
constexpr int MAX_PROPERTIES = 96;
constexpr int MAX_MEMBERS_PER_PROPERTY = 8;
```

If limits are exceeded, the app should not crash. It should drop excess properties and show a warning/status indicator if possible.

---

## 12. Device Classification

The project should discover devices generically, then classify them opportunistically.

A device may be considered a mount if it exposes one or more of:

* `EQUATORIAL_EOD_COORD`
* `TELESCOPE_ABORT_MOTION`
* `TELESCOPE_PARK`
* `TELESCOPE_TRACK_STATE`
* other standard telescope properties

A device may be considered a camera if it exposes:

* `CCD_EXPOSURE`
* `CCD_INFO`
* `CCD_CONTROLS`
* gain-like properties
* other standard CCD properties

A device may be considered a focuser if it exposes focuser-related properties.

A device may be considered a filter wheel if it exposes:

* `FILTER_SLOT`
* filter-name properties

Version 1 only needs polished UI for mount and camera.

Everything else can appear in the property inspector.

---

## 13. User Interface Direction

The UI should be a **minimal custom graphical UI** drawn directly with M5GFX.

Avoid:

* Heavy widget frameworks
* Image-heavy UI
* Dynamic heap-heavy screen systems
* Over-animated interfaces
* Trying to mimic a phone app

Use:

* Status bar
* Menus
* Highlighted rows
* Simple panels
* Clear state indicators
* Compact icons if useful
* Numeric/text editors
* Strong contrast
* Few screens
* Predictable keyboard shortcuts

The screen is small. Favor clarity over density.

---

## 14. UI Screens

### 14.1 Boot Screen

Shows:

* Project name
* Firmware version
* Wi-Fi status
* Optional splash/logo

Example:

```text
Cardputer INDI
Controller

Wi-Fi: connecting...
```

### 14.2 Wi-Fi / Server Screen

Allows:

* Show Wi-Fi connection status
* Choose or edit INDI server host/IP
* Choose or edit port, default `7624`
* Connect/disconnect from INDI

Version 1 may hardcode Wi-Fi credentials and server address during development. Later versions should support configuration through SD card or on-device editor.

### 14.3 Device List Screen

Shows discovered INDI devices:

```text
INDI Devices

> EQMod Mount      Mount
  ZWO CCD ASI...   Camera
  EAF Focuser      Other
```

Actions:

* Enter/select device
* Refresh properties
* Open raw inspector

### 14.4 Main Menu

Suggested top-level menu:

```text
INDI Controller

> Mount
  Camera
  Devices
  Properties
  Settings
```

### 14.5 Mount Screen

Shows:

* Selected mount name
* Connection state
* INDI property state
* RA/Dec if available
* Tracking state if available
* Park state if available
* Slew state if available

Actions:

* Connect/disconnect
* Slew to RA/Dec
* Abort motion
* Park/unpark
* Tracking on/off if available
* Nudge controls if standard motion properties are available

Do not assume all mounts expose every property.

### 14.6 Camera Screen

Shows:

* Selected camera name
* Connection state
* Exposure value
* Exposure state
* Gain/sensitivity if available
* Temperature/cooler state if available, read-only in v1 unless easy

Actions:

* Connect/disconnect
* Set exposure
* Start exposure
* Set gain if a writable gain-like property is available
* Show exposure progress if updates make this possible

### 14.7 Exposure Screen

A focused numeric editor:

```text
Exposure

Current: 5.0 s
New:     10.0 s

Enter = start
Esc   = cancel
```

Should allow fast values:

* 0.1
* 0.5
* 1
* 2
* 5
* 10
* 30
* 60

Number keys can be used for direct entry.

### 14.8 Property Inspector

This is the generic escape hatch.

Shows:

* Devices
* Groups
* Properties
* Members
* Type
* State
* Permission
* Current value

For writable properties, allow editing where simple:

* Number values
* Switch values
* Text values

The inspector should be paged and bounded. It should not attempt to render or expand the whole property tree at once.

---

## 15. Keyboard Mapping

Suggested controls:

* Arrow keys or equivalent: move selection
* Enter: select/confirm
* Backspace or Esc-equivalent: back/cancel
* Space: toggle selected switch
* Number keys: numeric entry / shortcuts
* `M`: mount screen
* `C`: camera screen
* `D`: devices
* `P`: properties
* `A`: abort mount motion when on mount screen, with confirmation if appropriate
* `R`: refresh/get properties
* `Q`: disconnect/back depending on context

Actual key constants should be verified with the M5Cardputer keyboard API.

The UI should not require complex key combinations.

---

## 16. INDI Connection Flow

On startup:

1. Initialize M5Cardputer hardware.
2. Initialize display.
3. Initialize keyboard.
4. Load config.
5. Connect to Wi-Fi.
6. Show server connection screen.
7. Connect TCP client to INDI server.
8. Send `getProperties`.
9. Disable BLOBs.
10. Parse incoming definitions.
11. Populate property cache.
12. Classify devices.
13. Show main menu or device list.

Connection should be recoverable. If TCP disconnects:

* Show disconnected state.
* Allow reconnect.
* Clear or mark property cache stale.
* Do not crash.

---

## 17. INDI Write Commands

The app should generate XML manually for the few command forms it needs.

Example number command:

```xml
<newNumberVector device="CCD Simulator" name="CCD_EXPOSURE">
  <oneNumber name="CCD_EXPOSURE_VALUE">5</oneNumber>
</newNumberVector>
```

Example switch command:

```xml
<newSwitchVector device="Mount" name="CONNECTION">
  <oneSwitch name="CONNECT">On</oneSwitch>
  <oneSwitch name="DISCONNECT">Off</oneSwitch>
</newSwitchVector>
```

The writer must escape XML text values correctly.

Numbers and switch names should come from the property cache, not from hardcoded assumptions, whenever possible.

---

## 18. Mount Feature Detection

Mount UI should be capability-driven.

### Connection

Find property:

* `CONNECTION`

Members usually:

* `CONNECT`
* `DISCONNECT`

Use actual member names from cache.

### Coordinates

Find property:

* `EQUATORIAL_EOD_COORD`

Members may include:

* `RA`
* `DEC`

The UI should not crash if member names differ. It may show raw member names if unknown.

### Slew Behavior

Find property:

* `ON_COORD_SET`

Common modes may include:

* `TRACK`
* `SLEW`
* `SYNC`

The UI can prefer `SLEW` for normal slew-to-coordinate behavior.

### Abort

Find property:

* `TELESCOPE_ABORT_MOTION`

Usually a switch vector. Use actual member(s).

### Park

Find property:

* `TELESCOPE_PARK`

Common members may include park/unpark variants. Use actual member names and labels.

### Tracking

Find property:

* `TELESCOPE_TRACK_STATE`

Use actual switch members.

---

## 19. Camera Feature Detection

### Exposure

Find property:

* `CCD_EXPOSURE`

Common member:

* `CCD_EXPOSURE_VALUE`

If there is exactly one number member, use it even if the member name differs.

Action:

* Set exposure number.
* Send `newNumberVector`.
* Track property state updates.

### Gain / Sensitivity

Gain is less standardized. Detect in this order:

1. A number property named `CCD_GAIN`.
2. A member named `GAIN` inside a camera controls property.
3. A number member with label/name containing `gain`.
4. A driver-specific likely gain/sensitivity property.

The UI should label this as “Gain” only when confident. Otherwise use the property/member label from INDI.

Do not invent ISO/gain mapping.

### Exposure Completion

For v1, do not download image BLOBs.

The app can report that exposure was started and monitor property state changes.

If the server attempts to send a BLOB anyway, skip it safely.

---

## 20. Configuration

Initial development may use compile-time constants:

```cpp
const char* WIFI_SSID = "...";
const char* WIFI_PASSWORD = "...";
const char* INDI_HOST = "192.168.1.50";
const int INDI_PORT = 7624;
```

But the project should move toward a config file on microSD:

```ini
wifi_ssid=Observatory
wifi_password=...
indi_host=192.168.1.50
indi_port=7624
```

The config file should not be committed if it contains secrets.

Suggested committed file:

```text
config.example.ini
```

Do not store private Wi-Fi credentials in the repository.

---

## 21. Logging and Debugging

The app should support serial logging.

Useful log categories:

* Wi-Fi
* TCP
* INDI RX
* INDI TX
* XML parser
* property cache
* UI navigation

Avoid logging huge XML payloads by default.

Provide a compile-time debug flag:

```cpp
#define INDI_DEBUG_XML 0
```

When enabled, it can log start/end elements and property names, not full BLOB/text payloads.

---

## 22. Testing Strategy

This project benefits from making the INDI parser testable away from hardware.

Where possible, keep the XML tokenizer and INDI parser independent from M5Stack APIs.

Suggested test approach:

* Store sample INDI XML streams in `test/indi_parser/sample_messages.xml`.
* Include samples split at awkward boundaries.
* Feed data to parser in random chunk sizes.
* Verify property cache contents.
* Verify that partial tags are handled.
* Verify that multiple messages in one chunk are handled.
* Verify that BLOB text is skipped.
* Verify that overlong strings are truncated safely.

Even if PlatformIO unit tests are not fully set up initially, keep the code structured so that a desktop harness can be added later.

---

## 23. Robustness Requirements

The app should handle:

* INDI server unavailable.
* Wi-Fi not connected.
* TCP disconnect.
* Empty property list.
* Multiple mounts.
* Multiple cameras.
* Unknown devices.
* Missing standard properties.
* Properties arriving after UI already opened.
* Driver-specific names/labels.
* Overlong XML attributes.
* Overlong text values.
* BLOBs accidentally arriving.
* Cache capacity exceeded.

Failure mode should be:

* Show warning.
* Skip unsafe/unknown data.
* Continue running.

Not:

* Crash.
* Reboot.
* Allocate until failure.
* Freeze UI permanently.

---

## 24. Memory Discipline

Guidelines:

* Prefer fixed-size buffers for names/labels.
* Avoid `String` in hot parser paths if possible.
* Avoid heap allocation while parsing.
* Avoid storing raw XML.
* Avoid storing BLOB data.
* Limit number of devices/properties/members.
* Truncate long labels safely.
* Use one current-message builder rather than many temporary objects.
* Keep UI screens lightweight.
* Redraw only the current screen.
* Do not store a full UI tree.

The UI can still be attractive. Boxes, text, highlights, and simple icons are cheap. The memory problem is not drawing a nice screen; the memory problem is unbounded XML or BLOB buffering.

---

## 25. Visual Style

The UI should feel like a dedicated instrument.

Desired qualities:

* Clear
* Calm
* High contrast
* Minimal
* Functional
* Slightly polished

Avoid visual clutter.

Suggested layout:

```text
┌────────────────────────┐
│ WiFi ● INDI ● BAT 78%  │
├────────────────────────┤
│ Mount: EQMod           │
│ RA  12:34:55           │
│ DEC +22°18'03"         │
│ State: Tracking        │
│                        │
│ > Slew                 │
│   Abort                │
│   Park                 │
└────────────────────────┘
```

Use color sparingly:

* Green/OK
* Yellow/busy/warning
* Red/alert/disconnected
* Blue/selection if desired

If color choices are abstracted in `Theme.h`, they can be tuned later.

---

## 26. MVP Definition

The MVP is complete when the Cardputer can:

1. Boot and show UI.
2. Connect to Wi-Fi.
3. Connect to an INDI server over TCP.
4. Send `getProperties`.
5. Disable BLOBs.
6. Parse incoming INDI property definitions and updates.
7. Maintain a bounded property cache.
8. List discovered devices.
9. Identify at least one mount and one camera from properties.
10. Show selected mount status.
11. Send connect/disconnect to a device through `CONNECTION`.
12. Show camera exposure property.
13. Set and start a camera exposure through `CCD_EXPOSURE`.
14. Show a generic property inspector.
15. Recover gracefully from server disconnect.

Mount slewing can be either part of MVP or MVP+1 depending on complexity.

---

## 27. MVP+1 Features

After MVP:

* Slew mount to RA/Dec.
* Abort motion.
* Park/unpark.
* Tracking toggle.
* Gain/sensitivity control.
* SD-card config.
* Multiple saved INDI servers.
* Better coordinate editor.
* Better raw property editor.
* On-device Wi-Fi/server setup.
* Battery indicator.
* Night mode / red theme.
* Persistent last selected mount/camera.
* Basic focuser support.
* Basic filter wheel support.

---

## 28. Out-of-Scope for Early Versions

Do not implement early:

* FITS parsing.
* BLOB image display.
* Plate solving.
* Guiding.
* Polar alignment.
* Star catalogs on device.
* Full object database.
* LX200/ST4/vendor protocols.
* Custom Raspberry Pi proxy.
* Touch UI assumptions.
* Cloud services.

A later version may optionally integrate with an HTTP helper on the Pi for thumbnails or object catalogs, but that is separate from the core generic INDI controller goal.

---

## 29. Suggested Implementation Order

### Phase 1: Hardware Skeleton

* Create PlatformIO project.
* Initialize M5Cardputer.
* Draw boot screen.
* Read keyboard.
* Implement simple screen navigation.
* Implement status bar.

### Phase 2: Wi-Fi and TCP

* Connect to Wi-Fi.
* Open TCP connection to configurable INDI host/port.
* Show connection status.
* Send a simple XML line such as `getProperties`.

### Phase 3: XML Tokenizer

* Implement streaming tokenizer.
* Feed bytes from TCP.
* Log start/end tags and attributes.
* Test split tags and arbitrary chunks.

### Phase 4: INDI Protocol Parser

* Recognize `def*Vector`, `set*Vector`, `one*`.
* Build/update property cache.
* Display raw device/property list.

### Phase 5: Device Classification

* Detect mount/camera candidates.
* Build simplified `MountState` and `CameraState` views on top of generic cache.

### Phase 6: Camera MVP

* Find `CCD_EXPOSURE`.
* Display current exposure.
* Edit exposure.
* Send `newNumberVector`.
* Monitor state.

### Phase 7: Mount MVP

* Find `CONNECTION`.
* Find coordinate/status properties.
* Show mount state.
* Implement safe commands:

  * connect/disconnect
  * abort
  * park/unpark if clear

### Phase 8: Polish

* Improve graphics.
* Improve keyboard shortcuts.
* Add SD config.
* Add errors/warnings.
* Add property inspector editing.
* Add gain detection.

---

## 30. Coding Style Preferences

Keep code explicit and readable.

Prefer small classes with clear responsibility.

Avoid clever abstractions.

Favor:

* Clear state machines
* Bounded buffers
* Deterministic behavior
* Simple debug logs
* Separate parser/model/UI layers

Avoid:

* Hidden global state beyond the central app object
* Heap-heavy parser paths
* Large inheritance hierarchies
* Magic property assumptions
* Blocking UI for long operations

The app loop should continue updating the UI and keyboard even while network data is arriving.

---

## 31. Possible Core Classes

### `App`

Owns global state and coordinates subsystems.

Responsibilities:

* Initialize hardware
* Run main loop
* Poll keyboard
* Poll network
* Update UI
* Handle high-level navigation

### `WifiManager`

Responsibilities:

* Connect Wi-Fi
* Report connection state
* Reconnect if needed

### `IndiClient`

Responsibilities:

* TCP connection to INDI server
* Send INDI XML commands
* Feed received bytes to parser
* Expose connection state

### `IndiXmlTokenizer`

Responsibilities:

* Streaming XML tokenization
* Preserve state across chunks
* Emit parser events

### `IndiProtocol`

Responsibilities:

* Convert XML events into INDI property definitions/updates
* Track current vector/member being parsed
* Update `IndiPropertyCache`

### `IndiPropertyCache`

Responsibilities:

* Store bounded devices/properties/members
* Find properties by device/name
* Find devices exposing standard properties
* Provide read-only views for UI

### `Ui`

Responsibilities:

* Manage current screen
* Dispatch key events
* Draw screen
* Draw status bar

---

## 32. Open Questions for Codex to Resolve During Implementation

* Exact PlatformIO board configuration for Cardputer ADV.
* Whether the specific Cardputer ADV hardware exposes PSRAM and how PlatformIO should configure it.
* Exact M5Cardputer keyboard constants for arrows, enter, backspace, Fn, etc.
* Best way to represent the small display orientation.
* Whether to use `WiFiClient` directly or a lower-level socket API.
* Whether an existing tiny SAX-like XML parser is worth using or whether a custom tokenizer is simpler.
* Correct exact INDI `enableBLOB` command form for disabling all BLOBs globally or per device.
* Best initial property-cache limits after measuring memory.
* Whether to add a desktop parser test harness early.

Codex should make pragmatic choices and document them.

---

## 33. Acceptance Criteria for First Real Demo

A useful first demo can use the INDI simulator drivers.

Example demo setup on Raspberry Pi or Linux machine:

```bash
indiserver indi_simulator_telescope indi_simulator_ccd
```

The Cardputer should:

* Connect to Wi-Fi.
* Connect to the INDI server.
* Discover simulator telescope and CCD.
* Show both devices.
* Show camera exposure property.
* Set an exposure time.
* Start an exposure.
* Show exposure state changes.
* Show at least some mount status.
* Allow viewing raw properties.

No physical telescope is required for this first demo.

---

## 34. Final Product Vision

The final product should feel like a small dedicated astronomical instrument:

* Turn it on.
* It joins the observatory Wi-Fi.
* It connects to the INDI server.
* It shows mount and camera status.
* It lets the user perform common actions quickly.
* It remains generic enough to work across many INDI drivers.
* It offers a raw property inspector for unusual devices.
* It does not try to be a laptop.

The best version of this project is not a tiny desktop application. It is a **well-designed handheld INDI controller**.

