#include <Arduino.h>
#include <M5Cardputer.h>
#include <WiFi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if __has_include("config.h")
#include "config.h"
#endif
#include "config_defaults.h"
#include "version.h"
#include "app/ConfigStore.h"
#include "app/RuntimeConfig.h"
#include "camera/CameraModel.h"
#include "gnss/GnssReceiver.h"
#include "indi/IndiProtocol.h"
#include "indi/IndiPropertyCache.h"
#include "indi/IndiWriter.h"
#include "mount/Astronomy.h"
#include "mount/MountModel.h"

namespace {

constexpr uint32_t kWifiRetryMs = 10000;
constexpr uint32_t kIndiRetryMs = 5000;
constexpr uint32_t kRedrawMs = 500;
constexpr uint32_t kFeedbackMs = 2500;
constexpr uint32_t kMotionReleaseDebounceMs = 75;
constexpr size_t kReadBufferSize = 512;
constexpr size_t kVisibleRows = 5;

indi::PropertyCache propertyCache;
indi::Protocol protocol(propertyCache);
WiFiClient indiClient;
M5Canvas canvas(&M5Cardputer.Display);

uint32_t lastWifiAttempt = 0;
uint32_t lastIndiAttempt = 0;
uint32_t lastRedraw = 0;
bool discoverySent = false;
bool wasConnected = false;
size_t loggedPropertyCount = 0;
size_t blobPolicyDeviceCount = 0;
app::ConfigStore configStore;
app::RuntimeConfig activeConfig;
app::RuntimeConfig editConfig;

enum class Screen {
  Devices,
  Properties,
  Members,
  Mount,
  MountGps,
  Camera,
  Gnss,
  Settings,
  WifiList,
  TextInput
};
Screen screen = Screen::Devices;
size_t selectedDevice = 0;
size_t selectedProperty = 0;
size_t selectedMember = 0;
double selectedExposure = 1.0;
enum class CameraCommandKind { None, Iso };
CameraCommandKind pendingCameraCommand = CameraCommandKind::None;
char pendingCameraMember[indi::kSwitchOptionNameSize]{};
uint32_t pendingCameraCommandUntil = 0;
char mountUtcSource[indi::kTextSize]{};
uint32_t mountUtcSourceMs = 0;

enum class AxisMotion { None, Negative, Positive };
AxisMotion nsMotion = AxisMotion::None;
AxisMotion weMotion = AxisMotion::None;
bool motionArmed = false;
bool nsReleasePending = false;
bool weReleasePending = false;
uint32_t nsReleaseSince = 0;
uint32_t weReleaseSince = 0;
char feedback[48]{};
uint32_t feedbackUntil = 0;
uint16_t feedbackColor = TFT_CYAN;
size_t selectedSetting = 0;
constexpr size_t kMaxScannedNetworks = 12;
char scannedSsids[kMaxScannedNetworks][app::kSsidSize]{};
size_t scannedNetworkCount = 0;
size_t selectedNetwork = 0;

enum class InputField { Password, Host, Port };
InputField inputField = InputField::Password;
char inputBuffer[app::kPasswordSize]{};
size_t inputLength = 0;

void draw();

template <size_t N>
void copyValue(char (&destination)[N], const char* source) {
  strncpy(destination, source ? source : "", N - 1);
  destination[N - 1] = '\0';
}

void loadConfiguration() {
  copyValue(activeConfig.ssid, WIFI_SSID);
  copyValue(activeConfig.password, WIFI_PASSWORD);
  copyValue(activeConfig.host, INDI_HOST);
  activeConfig.port = INDI_PORT;
  configStore.load(activeConfig);
  editConfig = activeConfig;
}

void setFeedback(const char* message, uint16_t color = TFT_CYAN) {
  strncpy(feedback, message ? message : "", sizeof(feedback) - 1);
  feedback[sizeof(feedback) - 1] = '\0';
  feedbackColor = color;
  feedbackUntil = millis() + kFeedbackMs;
  lastRedraw = 0;
}

bool hasFeedback() {
  if (!*feedback) return false;
  if (static_cast<int32_t>(feedbackUntil - millis()) > 0) return true;
  feedback[0] = '\0';
  return false;
}

const char* propertyTypeName(indi::PropertyType type) {
  switch (type) {
    case indi::PropertyType::Number: return "Number";
    case indi::PropertyType::Switch: return "Switch";
    case indi::PropertyType::Text: return "Text";
    case indi::PropertyType::Light: return "Light";
    case indi::PropertyType::Blob: return "BLOB";
    default: return "Unknown";
  }
}

const char* stateName(indi::State state) {
  switch (state) {
    case indi::State::Idle: return "Idle";
    case indi::State::Ok: return "Ok";
    case indi::State::Busy: return "Busy";
    case indi::State::Alert: return "Alert";
    default: return "Unknown";
  }
}

const char* permissionName(indi::Permission permission) {
  switch (permission) {
    case indi::Permission::ReadOnly: return "RO";
    case indi::Permission::WriteOnly: return "WO";
    case indi::Permission::ReadWrite: return "RW";
    default: return "--";
  }
}

const indi::Device* currentDevice() {
  return selectedDevice < propertyCache.deviceCount() ? &propertyCache.device(selectedDevice)
                                                       : nullptr;
}

const indi::Property* currentProperty() {
  const indi::Device* device = currentDevice();
  return device ? propertyCache.propertyForDevice(device->name, selectedProperty) : nullptr;
}

mount::Model currentMount() {
  const indi::Device* device = currentDevice();
  return mount::Model(propertyCache, device ? device->name : nullptr);
}

camera::Model currentCamera() {
  const indi::Device* device = currentDevice();
  return camera::Model(propertyCache, device ? device->name : nullptr);
}

void clampSelection() {
  if (screen == Screen::Gnss || screen == Screen::Settings || screen == Screen::WifiList ||
      screen == Screen::TextInput) {
    return;
  }
  if (propertyCache.deviceCount() == 0) {
    selectedDevice = selectedProperty = selectedMember = 0;
    screen = Screen::Devices;
    return;
  }
  if (selectedDevice >= propertyCache.deviceCount()) selectedDevice = propertyCache.deviceCount() - 1;
  const indi::Device* device = currentDevice();
  const size_t propertyCount = propertyCache.propertyCountForDevice(device->name);
  if (propertyCount == 0) {
    selectedProperty = selectedMember = 0;
    if (screen == Screen::Members) screen = Screen::Properties;
    else if (screen == Screen::Mount || screen == Screen::MountGps || screen == Screen::Camera)
      screen = Screen::Devices;
    return;
  }
  if (selectedProperty >= propertyCount) selectedProperty = propertyCount - 1;
  const indi::Property* property = currentProperty();
  if (!property || property->memberCount == 0) {
    selectedMember = 0;
    if (screen == Screen::Members) screen = Screen::Properties;
  } else if (selectedMember >= property->memberCount) {
    selectedMember = property->memberCount - 1;
  }
}

const char* wifiStatus() {
  if (!*activeConfig.ssid) return "not configured";
  switch (WiFi.status()) {
    case WL_CONNECTED: return "connected";
    case WL_NO_SSID_AVAIL: return "SSID missing";
    case WL_CONNECT_FAILED: return "failed";
    default: return "connecting";
  }
}

void beginWifi() {
  if (!*activeConfig.ssid || WiFi.status() == WL_CONNECTED) return;
  Serial.printf("[wifi] connecting to %s\n", activeConfig.ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(activeConfig.ssid, activeConfig.password);
  lastWifiAttempt = millis();
}

void pollWifi() {
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiAttempt >= kWifiRetryMs) {
    beginWifi();
  }
}

void sendDiscovery() {
  static const char getProperties[] = "<getProperties version=\"1.7\"/>\n";
  static const char disableBlobs[] = "<enableBLOB>Never</enableBLOB>\n";
  indiClient.write(reinterpret_cast<const uint8_t*>(getProperties), strlen(getProperties));
  indiClient.write(reinterpret_cast<const uint8_t*>(disableBlobs), strlen(disableBlobs));
  discoverySent = true;
  Serial.println("[indi] sent getProperties and enableBLOB Never");
}

void writeXmlEscaped(const char* value) {
  while (*value) {
    switch (*value) {
      case '&': indiClient.print("&amp;"); break;
      case '<': indiClient.print("&lt;"); break;
      case '>': indiClient.print("&gt;"); break;
      case '"': indiClient.print("&quot;"); break;
      case '\'': indiClient.print("&apos;"); break;
      default: indiClient.write(static_cast<uint8_t>(*value)); break;
    }
    ++value;
  }
}

void sendDeviceBlobPolicies() {
  if (blobPolicyDeviceCount == propertyCache.deviceCount()) return;
  for (size_t i = 0; i < propertyCache.deviceCount(); ++i) {
    indiClient.print("<enableBLOB device=\"");
    writeXmlEscaped(propertyCache.device(i).name);
    indiClient.print("\">Never</enableBLOB>\n");
  }
  blobPolicyDeviceCount = propertyCache.deviceCount();
  Serial.printf("[indi] disabled BLOBs for %u devices\n",
                static_cast<unsigned>(blobPolicyDeviceCount));
}

bool sendSwitchCommand(const indi::Property* property, const char* activeMember) {
  if (!property || property->type != indi::PropertyType::Switch ||
      property->permission == indi::Permission::ReadOnly || !indiClient.connected()) {
    return false;
  }
  char xml[1024];
  const size_t length = indi::Writer::buildSwitchVector(xml, sizeof(xml), *property, activeMember);
  if (!length) {
    Serial.printf("[indi] failed to serialize switch vector %s\n", property->name);
    return false;
  }
  const size_t written = indiClient.write(reinterpret_cast<const uint8_t*>(xml), length);
  if (written != length) {
    Serial.printf("[indi] incomplete write for switch vector %s\n", property->name);
    return false;
  }
  Serial.printf("[indi] sent %s.%s = %s\n", property->device, property->name,
                activeMember ? activeMember : "all off");
  return true;
}

bool sendSwitchSelection(const indi::Property* property, const char* member) {
  if (!property || !member || property->type != indi::PropertyType::Switch ||
      property->permission == indi::Permission::ReadOnly || !indiClient.connected()) {
    return false;
  }
  char xml[512];
  const size_t length = indi::Writer::buildSwitchMember(xml, sizeof(xml), *property, member, true);
  if (!length) return false;
  const size_t written = indiClient.write(reinterpret_cast<const uint8_t*>(xml), length);
  Serial.printf("[indi] sent %s.%s.%s = On (%u/%u bytes)\n", property->device, property->name,
                member, static_cast<unsigned>(written), static_cast<unsigned>(length));
  return written == length;
}

bool sendNumberCommand(const indi::Property* property, const char* member, double value) {
  if (!property || !member || property->type != indi::PropertyType::Number ||
      property->permission == indi::Permission::ReadOnly || !indiClient.connected()) {
    return false;
  }
  char xml[512];
  const size_t length = indi::Writer::buildNumberVector(xml, sizeof(xml), *property, member, value);
  if (!length) return false;
  return indiClient.write(reinterpret_cast<const uint8_t*>(xml), length) == length;
}

bool isWritable(const indi::Property* property, indi::PropertyType type) {
  return property && property->type == type &&
         (property->permission == indi::Permission::ReadWrite ||
          property->permission == indi::Permission::WriteOnly);
}

void syncMountFromGnss() {
  mount::Model model = currentMount();
  const gnss::Snapshot snapshot = gnss::current();
  const indi::Property* location = model.property("GEOGRAPHIC_COORD");
  const indi::Property* time = model.property("TIME_UTC");
  const indi::Member* latitude = model.member("GEOGRAPHIC_COORD", "LAT");
  const indi::Member* longitude = model.member("GEOGRAPHIC_COORD", "LONG");
  const indi::Member* elevation = model.member("GEOGRAPHIC_COORD", "ELEV");
  const indi::Member* utc = model.member("TIME_UTC", "UTC");
  const indi::Member* offset = model.member("TIME_UTC", "OFFSET");

  if (!indiClient.connected()) {
    setFeedback("INDI connection unavailable", TFT_RED);
    return;
  }
  if (!snapshot.present || !snapshot.fixValid || !snapshot.locationValid ||
      !snapshot.dateTimeValid) {
    setFeedback("Valid GPS fix and UTC required", TFT_YELLOW);
    return;
  }
  if (!isWritable(location, indi::PropertyType::Number) ||
      !isWritable(time, indi::PropertyType::Text) || !latitude || !longitude || !elevation ||
      !utc || !offset) {
    setFeedback("Mount location/time not writable", TFT_YELLOW);
    return;
  }

  char utcValue[32];
  snprintf(utcValue, sizeof(utcValue), "%04u-%02u-%02uT%02u:%02u:%02u", snapshot.year,
           snapshot.month, snapshot.day, snapshot.hour, snapshot.minute, snapshot.second);
  const indi::NumberValue locationValues[] = {
      {latitude->name, snapshot.latitude},
      {longitude->name, mount::longitudeToIndi(snapshot.longitude)},
      {elevation->name, elevation->numberValue},
  };
  const indi::TextValue timeValues[] = {{utc->name, utcValue}, {offset->name, "0"}};
  char locationXml[512];
  char timeXml[512];
  const size_t locationLength = indi::Writer::buildNumberVector(
      locationXml, sizeof(locationXml), *location, locationValues, 3);
  const size_t timeLength =
      indi::Writer::buildTextVector(timeXml, sizeof(timeXml), *time, timeValues, 2);
  if (!locationLength || !timeLength) {
    setFeedback("GPS sync serialization failed", TFT_RED);
    return;
  }

  const size_t locationWritten =
      indiClient.write(reinterpret_cast<const uint8_t*>(locationXml), locationLength);
  const size_t timeWritten = locationWritten == locationLength
                                 ? indiClient.write(reinterpret_cast<const uint8_t*>(timeXml),
                                                    timeLength)
                                 : 0;
  if (locationWritten != locationLength || timeWritten != timeLength) {
    setFeedback("GPS sync write failed", TFT_RED);
    return;
  }
  Serial.printf("[indi] sent GPS location and UTC to %s\n", location->device);
  setFeedback("GPS location and UTC sent");
}

void resetMountUtcClock() {
  mountUtcSource[0] = '\0';
  mountUtcSourceMs = millis();
}

bool currentMountUtc(char* output, size_t outputSize) {
  const indi::Member* utc = currentMount().member("TIME_UTC", "UTC");
  if (!utc || !*utc->text) return false;
  if (strcmp(mountUtcSource, utc->text) != 0) {
    copyValue(mountUtcSource, utc->text);
    mountUtcSourceMs = millis();
  }
  return mount::addUtcSeconds(mountUtcSource, (millis() - mountUtcSourceMs) / 1000, output,
                              outputSize);
}

const char* motionMember(const indi::Property* property, const char* preferred, size_t fallback) {
  if (!property) return nullptr;
  for (size_t i = 0; i < property->memberCount; ++i) {
    if (strcmp(property->members[i].name, preferred) == 0) return property->members[i].name;
  }
  return fallback < property->memberCount ? property->members[fallback].name : nullptr;
}

void setAxisMotion(const indi::Property* property, AxisMotion desired, AxisMotion& sent,
                   const char* negativeName, const char* positiveName,
                   const char* negativeFeedback, const char* positiveFeedback) {
  if (desired == sent) return;
  const char* active = nullptr;
  if (desired == AxisMotion::Negative) active = motionMember(property, negativeName, 0);
  else if (desired == AxisMotion::Positive) active = motionMember(property, positiveName, 1);
  if (sendSwitchCommand(property, active)) {
    sent = desired;
    setFeedback(desired == AxisMotion::None
                    ? "Motion stopped"
                    : (desired == AxisMotion::Negative ? negativeFeedback : positiveFeedback));
  } else {
    setFeedback("Motion command failed", TFT_RED);
  }
}

void stopMountMotion() {
  mount::Model model = currentMount();
  bool stopped = false;
  if (nsMotion != AxisMotion::None) {
    stopped |= sendSwitchCommand(model.property("TELESCOPE_MOTION_NS"), nullptr);
    nsMotion = AxisMotion::None;
  }
  if (weMotion != AxisMotion::None) {
    stopped |= sendSwitchCommand(model.property("TELESCOPE_MOTION_WE"), nullptr);
    weMotion = AxisMotion::None;
  }
  nsReleasePending = weReleasePending = false;
  if (stopped) setFeedback("Motion stopped");
}

AxisMotion debounceMotionRelease(AxisMotion desired, AxisMotion sent, bool keysReleased,
                                 bool& releasePending, uint32_t& releaseSince) {
  if (desired != AxisMotion::None || sent == AxisMotion::None || !keysReleased) {
    releasePending = false;
    return desired;
  }
  if (!releasePending) {
    releasePending = true;
    releaseSince = millis();
    return sent;
  }
  if (millis() - releaseSince < kMotionReleaseDebounceMs) return sent;
  releasePending = false;
  return AxisMotion::None;
}

void pollMountMotion() {
  if (screen != Screen::Mount || !indiClient.connected()) {
    nsMotion = weMotion = AxisMotion::None;
    motionArmed = false;
    nsReleasePending = weReleasePending = false;
    return;
  }
  const bool north = M5Cardputer.Keyboard.isKeyPressed(';');
  const bool south = M5Cardputer.Keyboard.isKeyPressed('.');
  const bool west = M5Cardputer.Keyboard.isKeyPressed(',');
  const bool east = M5Cardputer.Keyboard.isKeyPressed('/');
  if (!motionArmed) {
    nsReleasePending = weReleasePending = false;
    if (!north && !south && !west && !east) motionArmed = true;
    return;
  }

  const AxisMotion rawNS =
      north == south ? AxisMotion::None : (north ? AxisMotion::Negative : AxisMotion::Positive);
  const AxisMotion rawWE =
      west == east ? AxisMotion::None : (west ? AxisMotion::Negative : AxisMotion::Positive);
  const AxisMotion desiredNS =
      debounceMotionRelease(rawNS, nsMotion, !north && !south, nsReleasePending, nsReleaseSince);
  const AxisMotion desiredWE =
      debounceMotionRelease(rawWE, weMotion, !west && !east, weReleasePending, weReleaseSince);
  mount::Model model = currentMount();
  setAxisMotion(model.property("TELESCOPE_MOTION_NS"), desiredNS, nsMotion, "MOTION_NORTH",
                "MOTION_SOUTH", "Moving north", "Moving south");
  setAxisMotion(model.property("TELESCOPE_MOTION_WE"), desiredWE, weMotion, "MOTION_WEST",
                "MOTION_EAST", "Moving west", "Moving east");
}

void changeSlewRate(int direction) {
  mount::Model model = currentMount();
  const indi::Property* property = model.property("TELESCOPE_SLEW_RATE");
  const size_t count = model.slewRateCount();
  if (!property || count == 0) {
    setFeedback("Slew speed unavailable", TFT_YELLOW);
    return;
  }
  int index = model.activeSlewRateIndex();
  if (index < 0) index = 0;
  index += direction;
  if (index < 0) index = 0;
  if (index >= static_cast<int>(count)) index = static_cast<int>(count) - 1;
  const indi::Member* rate = model.slewRate(static_cast<size_t>(index));
  if (rate && sendSwitchCommand(property, rate->name)) {
    char message[48];
    snprintf(message, sizeof(message), "Speed: %.35s", *rate->label ? rate->label : rate->name);
    setFeedback(message);
  } else {
    setFeedback("Speed command failed", TFT_RED);
  }
}

void toggleDeviceConnection() {
  const indi::Device* device = currentDevice();
  const indi::Property* connection =
      device ? propertyCache.findProperty(device->name, "CONNECTION") : nullptr;
  if (!connection) {
    setFeedback("Connection unavailable", TFT_YELLOW);
    return;
  }
  const indi::Member* current = nullptr;
  for (size_t i = 0; i < connection->memberCount; ++i) {
    if (connection->members[i].active) current = &connection->members[i];
  }
  const bool connected = current && strcmp(current->name, "CONNECT") == 0;
  const char* target = motionMember(connection, connected ? "DISCONNECT" : "CONNECT",
                                    connected ? 1 : 0);
  if (sendSwitchCommand(connection, target)) {
    setFeedback(connected ? "Disconnect requested" : "Connect requested");
  } else {
    setFeedback("Connection command failed", TFT_RED);
  }
}

void initializeExposureSelection() {
  camera::Model model = currentCamera();
  const indi::Member* exposure = model.exposureMember();
  selectedExposure = exposure ? exposure->numberValue : 1.0;
  if (selectedExposure <= 0) selectedExposure = 1.0;
}

void beginCameraCommand(CameraCommandKind kind, const char* member) {
  pendingCameraCommand = kind;
  strncpy(pendingCameraMember, member, sizeof(pendingCameraMember) - 1);
  pendingCameraMember[sizeof(pendingCameraMember) - 1] = '\0';
  pendingCameraCommandUntil = millis() + 4000;
}

void pollCameraCommand() {
  if (pendingCameraCommand == CameraCommandKind::None) return;
  camera::Model model = currentCamera();
  const indi::SwitchOption* active = model.activeIso();
  if (active && strcmp(active->name, pendingCameraMember) == 0) {
    setFeedback("ISO acknowledged");
    pendingCameraCommand = CameraCommandKind::None;
  } else if (static_cast<int32_t>(millis() - pendingCameraCommandUntil) >= 0) {
    setFeedback("ISO not acknowledged", TFT_RED);
    pendingCameraCommand = CameraCommandKind::None;
  }
}

void changeExposure(int direction) {
  static const double presets[] = {0.1, 0.5, 1, 2, 5, 10, 30, 60};
  size_t nearest = 0;
  double distance = fabs(selectedExposure - presets[0]);
  for (size_t i = 1; i < sizeof(presets) / sizeof(presets[0]); ++i) {
    const double candidate = fabs(selectedExposure - presets[i]);
    if (candidate < distance) {
      distance = candidate;
      nearest = i;
    }
  }
  if (direction < 0 && nearest > 0) --nearest;
  else if (direction > 0 && nearest + 1 < sizeof(presets) / sizeof(presets[0])) ++nearest;
  selectedExposure = presets[nearest];
  char message[32];
  snprintf(message, sizeof(message), "Exposure: %.3g s", selectedExposure);
  setFeedback(message);
}

void changeIso(int direction) {
  camera::Model model = currentCamera();
  const indi::Property* iso = model.isoProperty();
  const size_t isoCount = model.isoCount();
  if (!iso || !isoCount) {
    setFeedback("ISO unavailable", TFT_YELLOW);
    return;
  }
  int index = model.activeIsoIndex();
  if (index < 0) index = 0;
  index += direction;
  if (index < 0) index = 0;
  if (index >= static_cast<int>(isoCount)) index = static_cast<int>(isoCount) - 1;
  const indi::SwitchOption& member = *model.iso(static_cast<size_t>(index));
  if (sendSwitchSelection(iso, member.name)) {
    beginCameraCommand(CameraCommandKind::Iso, member.name);
    char message[40];
    snprintf(message, sizeof(message), "ISO requested: %.22s",
             *member.label ? member.label : member.name);
    setFeedback(message);
  } else {
    setFeedback("ISO command failed", TFT_RED);
  }
}

void triggerCapture() {
  camera::Model model = currentCamera();
  const indi::Property* exposure = model.exposureProperty();
  const indi::Member* member = model.exposureMember();
  if (exposure && member && sendNumberCommand(exposure, member->name, selectedExposure)) {
    char message[40];
    snprintf(message, sizeof(message), "Capture started: %.3g s", selectedExposure);
    setFeedback(message);
    return;
  }
  setFeedback("Capture command failed", TFT_RED);
}

void abortMountMotion() {
  mount::Model model = currentMount();
  if (!model.isMount()) return;
  const indi::Property* abort = model.property("TELESCOPE_ABORT_MOTION");
  const char* member = motionMember(abort, "ABORT", 0);
  if (!abort || !member) {
    setFeedback("Abort unavailable", TFT_YELLOW);
    return;
  }
  if (sendSwitchCommand(abort, member)) {
    nsMotion = weMotion = AxisMotion::None;
    motionArmed = false;
    nsReleasePending = weReleasePending = false;
    setFeedback("ABORT requested", TFT_RED);
  } else {
    setFeedback("Abort command failed", TFT_RED);
  }
}

void pollIndi() {
  if (WiFi.status() != WL_CONNECTED) {
    if (indiClient.connected()) indiClient.stop();
    return;
  }

  if (!indiClient.connected()) {
    if (wasConnected) {
      Serial.println("[indi] disconnected; marking cache stale by clearing it");
      propertyCache.clear();
      protocol.reset();
      loggedPropertyCount = 0;
      blobPolicyDeviceCount = 0;
      wasConnected = false;
      discoverySent = false;
      nsMotion = weMotion = AxisMotion::None;
      motionArmed = false;
    }
    if (millis() - lastIndiAttempt >= kIndiRetryMs) {
      lastIndiAttempt = millis();
      Serial.printf("[indi] connecting to %s:%u\n", activeConfig.host, activeConfig.port);
      if (indiClient.connect(activeConfig.host, activeConfig.port, 1500)) {
        Serial.println("[indi] connected");
        wasConnected = true;
      }
    }
    return;
  }

  if (!discoverySent) sendDiscovery();

  char buffer[kReadBufferSize];
  while (indiClient.available()) {
    const int count = indiClient.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
    if (count > 0) protocol.feed(buffer, static_cast<size_t>(count));
  }
  if (propertyCache.propertyCount() != loggedPropertyCount) {
    loggedPropertyCount = propertyCache.propertyCount();
    Serial.printf("[indi] discovered %u devices and %u properties\n",
                  static_cast<unsigned>(propertyCache.deviceCount()),
                  static_cast<unsigned>(propertyCache.propertyCount()));
  }
  sendDeviceBlobPolicies();
}

void resetConnections() {
  indiClient.stop();
  WiFi.disconnect();
  propertyCache.clear();
  protocol.reset();
  discoverySent = false;
  wasConnected = false;
  loggedPropertyCount = 0;
  blobPolicyDeviceCount = 0;
  lastWifiAttempt = millis() - kWifiRetryMs;
  lastIndiAttempt = millis() - kIndiRetryMs;
}

void openSettings() {
  editConfig = activeConfig;
  selectedSetting = 0;
  screen = Screen::Settings;
  lastRedraw = 0;
}

void scanWifiNetworks() {
  setFeedback("Scanning WiFi...");
  scannedNetworkCount = 0;
  WiFi.mode(WIFI_STA);
  const int found = WiFi.scanNetworks(false, true);
  for (int i = 0; i < found && scannedNetworkCount < kMaxScannedNetworks; ++i) {
    const String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    bool duplicate = false;
    for (size_t existing = 0; existing < scannedNetworkCount; ++existing) {
      if (strcmp(scannedSsids[existing], ssid.c_str()) == 0) duplicate = true;
    }
    if (!duplicate) copyValue(scannedSsids[scannedNetworkCount++], ssid.c_str());
  }
  WiFi.scanDelete();
  selectedNetwork = 0;
  screen = Screen::WifiList;
  setFeedback(scannedNetworkCount ? "Select a WiFi network" : "No WiFi networks found",
              scannedNetworkCount ? TFT_CYAN : TFT_YELLOW);
}

void startTextInput(InputField field) {
  inputField = field;
  const char* value = "";
  if (field == InputField::Password) value = editConfig.password;
  else if (field == InputField::Host) value = editConfig.host;
  char port[8];
  if (field == InputField::Port) {
    snprintf(port, sizeof(port), "%u", editConfig.port);
    value = port;
  }
  copyValue(inputBuffer, value);
  inputLength = strlen(inputBuffer);
  screen = Screen::TextInput;
  lastRedraw = 0;
}

void commitTextInput() {
  if (inputField == InputField::Password) copyValue(editConfig.password, inputBuffer);
  else if (inputField == InputField::Host) copyValue(editConfig.host, inputBuffer);
  else {
    const long value = strtol(inputBuffer, nullptr, 10);
    if (value <= 0 || value > 65535) {
      setFeedback("Invalid port", TFT_RED);
      return;
    }
    editConfig.port = static_cast<uint16_t>(value);
  }
  screen = Screen::Settings;
  setFeedback("Value updated");
}

void reconnectActiveConfiguration() {
  resetConnections();
  beginWifi();
}

void testAndSaveConfiguration() {
  if (!app::validWifi(editConfig) || !app::validServer(editConfig)) {
    setFeedback("Complete WiFi and INDI settings", TFT_RED);
    return;
  }
  setFeedback("Testing WiFi...");
  draw();
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(editConfig.ssid, editConfig.password);
  const uint32_t deadline = millis() + 15000;
  while (WiFi.status() != WL_CONNECTED && static_cast<int32_t>(deadline - millis()) > 0) {
    M5Cardputer.update();
    delay(20);
  }
  if (WiFi.status() != WL_CONNECTED) {
    setFeedback("WiFi test failed", TFT_RED);
    reconnectActiveConfiguration();
    return;
  }
  WiFiClient probe;
  setFeedback("Testing INDI server...");
  draw();
  if (!probe.connect(editConfig.host, editConfig.port, 3000)) {
    setFeedback("INDI server test failed", TFT_RED);
    reconnectActiveConfiguration();
    return;
  }
  probe.stop();
  if (!configStore.save(editConfig)) {
    setFeedback("Could not save settings", TFT_RED);
    reconnectActiveConfiguration();
    return;
  }
  activeConfig = editConfig;
  resetConnections();
  screen = Screen::Devices;
  setFeedback("Settings saved");
}

void clearSavedConfiguration() {
  if (!configStore.clear()) {
    setFeedback("Could not clear settings", TFT_RED);
    return;
  }
  copyValue(activeConfig.ssid, WIFI_SSID);
  copyValue(activeConfig.password, WIFI_PASSWORD);
  copyValue(activeConfig.host, INDI_HOST);
  activeConfig.port = INDI_PORT;
  editConfig = activeConfig;
  resetConnections();
  setFeedback("Saved settings cleared");
}

void drawStatusBar() {
  auto& display = canvas;
  const gnss::Snapshot gnssSnapshot = gnss::current();
  display.fillRect(0, 0, display.width(), 18, TFT_DARKGREY);
  display.setTextColor(WiFi.status() == WL_CONNECTED ? TFT_GREEN : TFT_RED, TFT_DARKGREY);
  display.setCursor(3, 4);
  display.print("WiFi");
  display.setTextColor(indiClient.connected() ? TFT_GREEN : TFT_RED, TFT_DARKGREY);
  display.setCursor(48, 4);
  display.print("INDI");
  const uint16_t gnssColor = !gnssSnapshot.present
                                 ? TFT_RED
                                 : (gnssSnapshot.fixDimension == gnss::FixDimension::ThreeD
                                        ? TFT_GREEN
                                        : TFT_YELLOW);
  display.setTextColor(gnssColor, TFT_DARKGREY);
  display.setCursor(91, 4);
  display.print("GNSS");
  display.setTextColor(TFT_WHITE, TFT_DARKGREY);
  display.setCursor(126, 4);
  display.printf("%u/%u", static_cast<unsigned>(propertyCache.deviceCount()),
                 static_cast<unsigned>(propertyCache.propertyCount()));
  const int batteryLevel = M5.Power.getBatteryLevel();
  display.setTextColor(TFT_BLACK, TFT_DARKGREY);
  display.setTextDatum(middle_right);
  char batteryText[6];
  if (batteryLevel >= 0 && batteryLevel <= 100) snprintf(batteryText, sizeof(batteryText), "%d%%", batteryLevel);
  else strcpy(batteryText, "--%");
  display.drawString(batteryText, 237, 9);
  display.setTextDatum(top_left);
}

void drawHeader(const char* title, const char* hint) {
  auto& display = canvas;
  display.fillScreen(TFT_BLACK);
  drawStatusBar();
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setCursor(4, 23);
  display.print(title);
  const bool showingFeedback = hasFeedback();
  display.setTextColor(showingFeedback ? feedbackColor : TFT_DARKGREY, TFT_BLACK);
  display.setCursor(4, display.height() - 11);
  display.print(showingFeedback ? feedback : hint);
}

void drawDevices() {
  auto& display = canvas;
  const gnss::Snapshot gnssSnapshot = gnss::current();
  drawHeader("INDI Devices",
             gnssSnapshot.present ? "Arrows/open | G GNSS | S Settings"
                                  : "Arrows move/open | S Settings");
  if (propertyCache.deviceCount() == 0) {
    display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    display.setCursor(4, 43);
    if (!*activeConfig.ssid) display.println("Press S to configure");
    else if (WiFi.status() != WL_CONNECTED) display.printf("WiFi: %s", wifiStatus());
    else if (!indiClient.connected()) display.println("Connecting to server...");
    else display.println("Waiting for properties...");
  } else {
    const size_t first = (selectedDevice / kVisibleRows) * kVisibleRows;
    for (size_t i = first; i < propertyCache.deviceCount() && i < first + kVisibleRows; ++i) {
      const indi::Device& device = propertyCache.device(i);
      display.setCursor(4, 42 + static_cast<int>(i - first) * 15);
      display.setTextColor(i == selectedDevice ? TFT_CYAN : TFT_WHITE, TFT_BLACK);
      display.printf("%c %-24.24s %u", i == selectedDevice ? '>' : ' ', device.name,
                     static_cast<unsigned>(device.propertyCount));
    }
  }
}

void drawGnss() {
  auto& display = canvas;
  const gnss::Snapshot snapshot = gnss::current();
  drawHeader("GNSS", "Backspace returns to devices");
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setCursor(4, 39);
  if (snapshot.present) {
    display.printf("Module: detected @ %lu baud", static_cast<unsigned long>(snapshot.baudRate));
  } else if (snapshot.lastMessageMs) {
    display.printf("Module: stale (%lu s)", static_cast<unsigned long>(snapshot.messageAgeMs / 1000));
  } else {
    display.print("Module: waiting for NMEA");
  }
  display.setCursor(4, 54);
  const char* fix =
      snapshot.fixDimension == gnss::FixDimension::ThreeD
          ? "3D"
          : (snapshot.fixDimension == gnss::FixDimension::TwoD ? "2D" : "none");
  display.printf("Fix: %-4s  Satellites: %u", fix,
                 static_cast<unsigned>(snapshot.satellites));
  display.setCursor(4, 69);
  if (snapshot.dateTimeValid) {
    display.printf("UTC: %04u-%02u-%02u %02u:%02u:%02u", snapshot.year, snapshot.month,
                   snapshot.day, snapshot.hour, snapshot.minute, snapshot.second);
  } else {
    display.print("UTC: --");
  }
  display.setCursor(4, 84);
  if (snapshot.locationValid) display.printf("Lat: %+.6f", snapshot.latitude);
  else display.print("Lat: --");
  display.setCursor(4, 99);
  if (snapshot.locationValid) display.printf("Lon: %+.6f", snapshot.longitude);
  else display.print("Lon: --");
  display.setCursor(164, 54);
  if (snapshot.hdopValid) display.printf("HDOP %.1f", snapshot.hdop);
  else display.print("HDOP --");
  display.setCursor(164, 84);
  if (snapshot.fixDimension == gnss::FixDimension::ThreeD && snapshot.altitudeValid)
    display.printf("Elev %.1fm", snapshot.altitudeMeters);
  display.setCursor(4, 114);
  if (*snapshot.firmwareVersion) display.printf("FW: %.35s", snapshot.firmwareVersion);
  else display.print("FW: --");
}

void drawProperties() {
  auto& display = canvas;
  const indi::Device* device = currentDevice();
  if (!device) {
    screen = Screen::Devices;
    drawDevices();
    return;
  }
  char title[40];
  snprintf(title, sizeof(title), "%.30s properties", device->name);
  drawHeader(title, "Arrows move/open/back");
  const size_t count = propertyCache.propertyCountForDevice(device->name);
  const size_t first = (selectedProperty / kVisibleRows) * kVisibleRows;
  for (size_t i = first; i < count && i < first + kVisibleRows; ++i) {
    const indi::Property* property = propertyCache.propertyForDevice(device->name, i);
    display.setCursor(4, 42 + static_cast<int>(i - first) * 15);
    display.setTextColor(i == selectedProperty ? TFT_CYAN : TFT_WHITE, TFT_BLACK);
    display.printf("%c %-25.25s %-6s", i == selectedProperty ? '>' : ' ', property->name,
                   stateName(property->state));
  }
}

void drawMembers() {
  auto& display = canvas;
  const indi::Property* property = currentProperty();
  if (!property) {
    screen = Screen::Properties;
    drawProperties();
    return;
  }
  char title[40];
  snprintf(title, sizeof(title), "%.26s [%s %s]", property->name,
           propertyTypeName(property->type), permissionName(property->permission));
  drawHeader(title, "Arrows move/back");
  const size_t first = (selectedMember / kVisibleRows) * kVisibleRows;
  for (size_t i = first; i < property->memberCount && i < first + kVisibleRows; ++i) {
    const indi::Member& member = property->members[i];
    display.setCursor(4, 42 + static_cast<int>(i - first) * 15);
    display.setTextColor(i == selectedMember ? TFT_CYAN : TFT_WHITE, TFT_BLACK);
    display.printf("%c %-20.20s ", i == selectedMember ? '>' : ' ', member.name);
    if (property->type == indi::PropertyType::Number) display.printf("%.5g", member.numberValue);
    else if (property->type == indi::PropertyType::Switch) display.print(member.active ? "On" : "Off");
    else display.printf("%.20s", member.text);
  }
  if (property->memberCount == 0) {
    display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    display.setCursor(4, 43);
    display.print("No cached members");
  }
}

void formatSexagesimal(char* output, size_t size, double value, bool hours, bool showSign) {
  const bool negative = value < 0;
  double absolute = fabs(value);
  long totalSeconds = lround(absolute * 3600.0);
  if (hours) totalSeconds %= 24L * 3600L;
  const long major = totalSeconds / 3600L;
  const long minutes = (totalSeconds / 60L) % 60L;
  const long seconds = totalSeconds % 60L;
  snprintf(output, size, "%c%02ld:%02ld:%02ld",
           negative ? '-' : (showSign ? '+' : ' '), major, minutes, seconds);
}

const char* displayMemberName(const indi::Member* member) {
  if (!member) return "--";
  return *member->label ? member->label : member->name;
}

const char* displaySwitchOptionName(const indi::SwitchOption* option) {
  if (!option) return "--";
  return *option->label ? option->label : option->name;
}

void drawMount() {
  auto& display = canvas;
  const indi::Device* device = currentDevice();
  mount::Model model = currentMount();
  if (!device || !model.isMount()) {
    screen = Screen::Devices;
    drawDevices();
    return;
  }
  drawHeader(device->name, "Arrows | [] speed | G GPS | C | A!");

  const indi::Member* connection = model.activeMember("CONNECTION");
  const bool connected = connection && strcmp(connection->name, "CONNECT") == 0;
  const indi::Member* trackState = model.activeMember("TELESCOPE_TRACK_STATE");
  const indi::Member* trackMode = model.activeMember("TELESCOPE_TRACK_MODE");
  if (!trackMode) trackMode = model.activeMember("CELESTRON_TRACK_MODE");
  const char* tracking = "--";
  if (trackState) tracking = strcmp(trackState->name, "TRACK_ON") == 0 ? "On" : "Off";
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setCursor(4, 40);
  display.printf("Device: %-4.4s Track: %-4.4s", connected ? "On" : "Off", tracking);
  display.setCursor(4, 55);
  display.printf("Mode: %-26.26s", displayMemberName(trackMode));

  char ra[16] = "--";
  char dec[16] = "--";
  const indi::Member* raMember = model.member("EQUATORIAL_EOD_COORD", "RA");
  const indi::Member* decMember = model.member("EQUATORIAL_EOD_COORD", "DEC");
  if (raMember) formatSexagesimal(ra, sizeof(ra), raMember->numberValue, true, false);
  if (decMember) formatSexagesimal(dec, sizeof(dec), decMember->numberValue, false, true);
  display.setCursor(4, 70);
  display.printf("RA %s  DEC %s", ra, dec);

  char alt[16] = "--";
  char az[16] = "--";
  const indi::Member* altMember = model.member("HORIZONTAL_COORD", "ALT");
  const indi::Member* azMember = model.member("HORIZONTAL_COORD", "AZ");
  double altitude = altMember ? altMember->numberValue : 0;
  double azimuth = azMember ? azMember->numberValue : 0;
  bool haveHorizontal = altMember && azMember;
  if (!haveHorizontal && raMember && decMember) {
    const indi::Member* latitude = model.member("GEOGRAPHIC_COORD", "LAT");
    const indi::Member* longitude = model.member("GEOGRAPHIC_COORD", "LONG");
    const indi::Member* utc = model.member("TIME_UTC", "UTC");
    if (latitude && longitude && utc) {
      haveHorizontal = mount::equatorialToHorizontal(
          raMember->numberValue, decMember->numberValue, latitude->numberValue,
          longitude->numberValue, utc->text, altitude, azimuth);
    }
  }
  if (haveHorizontal) {
    formatSexagesimal(alt, sizeof(alt), altitude, false, true);
    formatSexagesimal(az, sizeof(az), azimuth, false, false);
  }
  display.setCursor(4, 85);
  display.printf("ALT %s  AZ %s", alt, az);

  const indi::Member* speed = model.activeMember("TELESCOPE_SLEW_RATE");
  display.setCursor(4, 100);
  display.printf("Speed: %-18.18s", displayMemberName(speed));
}

void drawMountGps() {
  auto& display = canvas;
  const indi::Device* device = currentDevice();
  mount::Model model = currentMount();
  if (!device || !model.isMount()) {
    screen = Screen::Devices;
    drawDevices();
    return;
  }
  drawHeader("Mount GPS Sync", "Enter sends GPS | Backspace mount");
  const indi::Member* latitude = model.member("GEOGRAPHIC_COORD", "LAT");
  const indi::Member* longitude = model.member("GEOGRAPHIC_COORD", "LONG");
  const gnss::Snapshot snapshot = gnss::current();

  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setCursor(4, 39);
  if (latitude) display.printf("Mount LAT: %+.6f", latitude->numberValue);
  else display.print("Mount LAT: --");
  display.setCursor(4, 54);
  if (longitude) display.printf("Mount LON: %+.6f", mount::longitudeFromIndi(longitude->numberValue));
  else display.print("Mount LON: --");
  display.setCursor(4, 69);
  char mountUtc[32];
  if (currentMountUtc(mountUtc, sizeof(mountUtc))) display.printf("Mount UTC: %s", mountUtc);
  else display.print("Mount UTC: --");

  display.setTextColor(snapshot.locationValid ? TFT_CYAN : TFT_YELLOW, TFT_BLACK);
  display.setCursor(4, 84);
  if (snapshot.locationValid) display.printf("GPS   LAT: %+.6f", snapshot.latitude);
  else display.print("GPS   LAT: --");
  display.setCursor(4, 99);
  if (snapshot.locationValid) display.printf("GPS   LON: %+.6f", snapshot.longitude);
  else display.print("GPS   LON: --");
  display.setCursor(4, 114);
  if (snapshot.dateTimeValid) {
    display.printf("GPS UTC: %04u-%02u-%02uT%02u:%02u:%02u", snapshot.year, snapshot.month,
                   snapshot.day, snapshot.hour, snapshot.minute, snapshot.second);
  } else {
    display.print("GPS UTC: --");
  }
}

void drawCamera() {
  auto& display = canvas;
  const indi::Device* device = currentDevice();
  camera::Model model = currentCamera();
  if (!device || !model.isCamera()) {
    screen = Screen::Devices;
    drawDevices();
    return;
  }
  drawHeader(device->name, "[] Exp | -= ISO | Space snap | C");
  const indi::Property* connection = model.property("CONNECTION");
  const indi::Member* connectedMember = nullptr;
  if (connection) {
    for (size_t i = 0; i < connection->memberCount; ++i) {
      if (connection->members[i].active) connectedMember = &connection->members[i];
    }
  }
  const bool connected = connectedMember && strcmp(connectedMember->name, "CONNECT") == 0;
  const indi::Property* exposureProperty = model.exposureProperty();
  const indi::Member* exposure = model.exposureMember();
  const indi::SwitchOption* iso = model.activeIso();

  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setCursor(4, 42);
  display.printf("Device: %-4s  State: %-7s", connected ? "On" : "Off",
                 exposureProperty ? stateName(exposureProperty->state) : "--");
  display.setCursor(4, 61);
  display.printf("Selected exposure: %.3g s", selectedExposure);
  display.setCursor(4, 80);
  if (exposure) display.printf("Camera exposure: %.3g s", exposure->numberValue);
  else display.print("Camera exposure: --");
  display.setCursor(4, 99);
  display.printf("ISO: %-25.25s", displaySwitchOptionName(iso));
}

void drawSettings() {
  auto& display = canvas;
  drawHeader("Settings", "Arrows open/back");
  display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  display.setTextDatum(top_right);
  display.drawString(FW_VERSION, display.width() - 4, 23);
  display.setTextDatum(top_left);
  constexpr size_t count = 6;
  const size_t first = (selectedSetting / kVisibleRows) * kVisibleRows;
  for (size_t i = first; i < count && i < first + kVisibleRows; ++i) {
    display.setCursor(4, 42 + static_cast<int>(i - first) * 15);
    display.setTextColor(i == selectedSetting ? TFT_CYAN : TFT_WHITE, TFT_BLACK);
    display.print(i == selectedSetting ? "> " : "  ");
    switch (i) {
      case 0: display.printf("WiFi: %.28s", *editConfig.ssid ? editConfig.ssid : "--"); break;
      case 1: display.printf("Password: %s", *editConfig.password ? "********" : "(empty)"); break;
      case 2: display.printf("INDI host: %.25s", *editConfig.host ? editConfig.host : "--"); break;
      case 3: display.printf("INDI port: %u", editConfig.port); break;
      case 4: display.print("Test & save"); break;
      case 5: display.print("Clear saved settings"); break;
    }
  }
}

void drawWifiList() {
  auto& display = canvas;
  drawHeader("Select WiFi", "Arrows select/back");
  if (!scannedNetworkCount) {
    display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    display.setCursor(4, 43);
    display.print("No networks found");
    return;
  }
  const size_t first = (selectedNetwork / kVisibleRows) * kVisibleRows;
  for (size_t i = first; i < scannedNetworkCount && i < first + kVisibleRows; ++i) {
    display.setCursor(4, 42 + static_cast<int>(i - first) * 15);
    display.setTextColor(i == selectedNetwork ? TFT_CYAN : TFT_WHITE, TFT_BLACK);
    display.printf("%c %.32s", i == selectedNetwork ? '>' : ' ', scannedSsids[i]);
  }
}

void drawTextInput() {
  auto& display = canvas;
  const char* title = inputField == InputField::Password
                          ? "WiFi password"
                          : (inputField == InputField::Host ? "INDI host" : "INDI port");
  drawHeader(title, "Enter save | Tab cancel | Del");
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setCursor(4, 48);
  if (inputField == InputField::Password) {
    for (size_t i = 0; i < inputLength && i < 36; ++i) display.print('*');
  } else {
    const size_t first = inputLength > 36 ? inputLength - 36 : 0;
    display.print(inputBuffer + first);
  }
  display.setTextColor(TFT_CYAN, TFT_BLACK);
  display.print("_");
}

void draw() {
  clampSelection();
  switch (screen) {
    case Screen::Devices: drawDevices(); break;
    case Screen::Properties: drawProperties(); break;
    case Screen::Members: drawMembers(); break;
    case Screen::Mount: drawMount(); break;
    case Screen::MountGps: drawMountGps(); break;
    case Screen::Camera: drawCamera(); break;
    case Screen::Gnss: drawGnss(); break;
    case Screen::Settings: drawSettings(); break;
    case Screen::WifiList: drawWifiList(); break;
    case Screen::TextInput: drawTextInput(); break;
  }
  auto& display = canvas;
  if (propertyCache.overflowed() || protocol.overflowed()) {
    display.setTextColor(TFT_YELLOW, TFT_BLACK);
    display.setCursor(display.width() - 10, 4);
    display.print("!");
  }
  canvas.pushSprite(0, 0);
}

void moveSelection(int direction) {
  clampSelection();
  size_t* selected = nullptr;
  size_t count = 0;
  if (screen == Screen::Devices) {
    selected = &selectedDevice;
    count = propertyCache.deviceCount();
  } else if (screen == Screen::Properties) {
    selected = &selectedProperty;
    const indi::Device* device = currentDevice();
    count = device ? propertyCache.propertyCountForDevice(device->name) : 0;
  } else {
    selected = &selectedMember;
    const indi::Property* property = currentProperty();
    count = property ? property->memberCount : 0;
  }
  if (!selected || count == 0) return;
  if (direction < 0) *selected = *selected == 0 ? count - 1 : *selected - 1;
  else *selected = (*selected + 1) % count;
}

void handleKeyboard() {
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
  auto& keys = M5Cardputer.Keyboard.keysState();

  if (screen == Screen::TextInput) {
    if (keys.tab) {
      screen = Screen::Settings;
      setFeedback("Edit cancelled");
      return;
    }
    if (keys.enter) {
      commitTextInput();
      return;
    }
    if (keys.del && inputLength) inputBuffer[--inputLength] = '\0';
    for (char key : keys.word) {
      if (inputField == InputField::Port && (key < '0' || key > '9')) continue;
      if (inputLength + 1 < sizeof(inputBuffer)) {
        inputBuffer[inputLength++] = key;
        inputBuffer[inputLength] = '\0';
      }
    }
    lastRedraw = 0;
    return;
  }

  bool handled = false;
  bool open = keys.enter;
  bool back = keys.del;
  bool emergencyAbort = false;
  int direction = 0;
  for (char key : keys.word) {
    if (key == 'a' || key == 'A') emergencyAbort = true;
    if ((key == 'g' || key == 'G') && screen == Screen::Devices && gnss::current().present) {
      screen = Screen::Gnss;
      lastRedraw = 0;
      return;
    }
    if ((key == 'g' || key == 'G') && screen == Screen::Mount) {
      stopMountMotion();
      motionArmed = false;
      resetMountUtcClock();
      screen = Screen::MountGps;
      lastRedraw = 0;
      return;
    }
    if ((key == 's' || key == 'S') && screen == Screen::Devices) {
      openSettings();
      return;
    }
    if (screen == Screen::Mount) {
      if (key == '[') {
        changeSlewRate(-1);
        handled = true;
      } else if (key == ']') {
        changeSlewRate(1);
        handled = true;
      } else if (key == 'c' || key == 'C') {
        stopMountMotion();
        toggleDeviceConnection();
        handled = true;
      }
    } else if (screen == Screen::MountGps) {
      // Only Enter sends and Backspace returns from the comparison screen.
    } else if (screen == Screen::Camera) {
      if (key == '[') {
        changeExposure(-1);
        handled = true;
      } else if (key == ']') {
        changeExposure(1);
        handled = true;
      } else if (key == '-') {
        changeIso(-1);
        handled = true;
      } else if (key == '=') {
        changeIso(1);
        handled = true;
      } else if (key == 'c' || key == 'C') {
        toggleDeviceConnection();
        handled = true;
      }
    } else {
      if (key == ';') direction = -1;
      else if (key == '.') direction = 1;
      else if (key == '/') open = true;
      else if (key == ',') back = true;
    }
  }

  if (screen == Screen::Settings) {
    if (direction < 0) selectedSetting = selectedSetting == 0 ? 5 : selectedSetting - 1;
    else if (direction > 0) selectedSetting = (selectedSetting + 1) % 6;
    else if (back) {
      screen = Screen::Devices;
    } else if (open) {
      if (selectedSetting == 0) scanWifiNetworks();
      else if (selectedSetting == 1) startTextInput(InputField::Password);
      else if (selectedSetting == 2) startTextInput(InputField::Host);
      else if (selectedSetting == 3) startTextInput(InputField::Port);
      else if (selectedSetting == 4) testAndSaveConfiguration();
      else clearSavedConfiguration();
    }
    lastRedraw = 0;
    return;
  }

  if (screen == Screen::WifiList) {
    if (direction < 0 && scannedNetworkCount)
      selectedNetwork = selectedNetwork == 0 ? scannedNetworkCount - 1 : selectedNetwork - 1;
    else if (direction > 0 && scannedNetworkCount)
      selectedNetwork = (selectedNetwork + 1) % scannedNetworkCount;
    else if (back) {
      screen = Screen::Settings;
    } else if (open && scannedNetworkCount) {
      copyValue(editConfig.ssid, scannedSsids[selectedNetwork]);
      editConfig.password[0] = '\0';
      startTextInput(InputField::Password);
    }
    lastRedraw = 0;
    return;
  }

  if (emergencyAbort) {
    abortMountMotion();
    handled = true;
  } else if (open) {
    if (screen == Screen::Devices && currentDevice()) {
      if (currentMount().isMount()) {
        screen = Screen::Mount;
        motionArmed = false;
      } else if (currentCamera().isCamera()) {
        screen = Screen::Camera;
        initializeExposureSelection();
      } else {
        screen = Screen::Properties;
        selectedProperty = 0;
      }
      handled = true;
    } else if (screen == Screen::Properties && currentProperty()) {
      screen = Screen::Members;
      selectedMember = 0;
      handled = true;
    } else if (screen == Screen::Mount) {
      stopMountMotion();
      motionArmed = false;
      screen = Screen::Properties;
      selectedProperty = 0;
      handled = true;
    } else if (screen == Screen::MountGps) {
      syncMountFromGnss();
      handled = true;
    } else if (screen == Screen::Camera) {
      screen = Screen::Properties;
      selectedProperty = 0;
      handled = true;
    }
  } else if (back) {
    if (screen == Screen::Members) {
      screen = Screen::Properties;
    } else if (screen == Screen::Properties) {
      screen = Screen::Devices;
    } else if (screen == Screen::Mount) {
      stopMountMotion();
      motionArmed = false;
      screen = Screen::Devices;
    } else if (screen == Screen::MountGps) {
      screen = Screen::Mount;
      motionArmed = false;
    } else if (screen == Screen::Camera) {
      screen = Screen::Devices;
    } else if (screen == Screen::Gnss) {
      screen = Screen::Devices;
    }
    handled = true;
  } else if (screen == Screen::Camera && keys.space) {
    triggerCapture();
    handled = true;
  } else if (direction != 0) {
    moveSelection(direction);
    handled = true;
  }
  if (handled) lastRedraw = 0;
}

}  // namespace

void setup() {
  auto config = M5.config();
  M5Cardputer.begin(config, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextFont(1);
  canvas.setColorDepth(16);
  canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
  canvas.setTextSize(1);
  canvas.setTextFont(1);
  Serial.begin(115200);
  delay(100);
  Serial.println("[app] Cardputer INDI Controller");
  if (!gnss::begin()) Serial.println("[gnss] failed to start receiver task");
  loadConfiguration();
  if (app::validWifi(activeConfig) && app::validServer(activeConfig)) beginWifi();
  else openSettings();
  draw();
}

void loop() {
  M5Cardputer.update();
  pollWifi();
  pollIndi();
  pollCameraCommand();
  handleKeyboard();
  pollMountMotion();
  if (millis() - lastRedraw >= kRedrawMs) {
    draw();
    lastRedraw = millis();
  }
  delay(1);
}
