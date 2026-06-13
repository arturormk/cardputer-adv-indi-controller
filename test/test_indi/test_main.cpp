#include <unity.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "indi/IndiProtocol.h"
#include "indi/IndiWriter.h"
#include "mount/MountModel.h"
#include "mount/Astronomy.h"
#include "app/RuntimeConfig.h"
#include "camera/CameraModel.h"
#include "gnss/GnssState.h"

using namespace indi;

static PropertyCache cache;
static Protocol protocol(cache);

void setUp() {
  cache.clear();
  protocol.reset();
}

void tearDown() {}

static void feedChunks(const char* xml, size_t chunk) {
  const size_t length = strlen(xml);
  for (size_t offset = 0; offset < length; offset += chunk) {
    const size_t remaining = length - offset;
    protocol.feed(xml + offset, remaining < chunk ? remaining : chunk);
  }
}

void test_parses_definitions_across_single_byte_chunks() {
  const char* xml =
      "<defNumberVector device=\"CCD Simulator\" name=\"CCD_EXPOSURE\" label=\"Exposure\" "
      "group=\"Main Control\" state=\"Idle\" perm=\"rw\">"
      "<defNumber name=\"CCD_EXPOSURE_VALUE\" label=\"Duration\" min=\"0.01\" max=\"3600\" "
      "step=\"0.01\">1.5</defNumber></defNumberVector>";
  feedChunks(xml, 1);

  TEST_ASSERT_EQUAL_UINT(1, cache.deviceCount());
  const Property* property = cache.findProperty("CCD Simulator", "CCD_EXPOSURE");
  TEST_ASSERT_NOT_NULL(property);
  TEST_ASSERT_EQUAL(PropertyType::Number, property->type);
  TEST_ASSERT_EQUAL(State::Idle, property->state);
  TEST_ASSERT_EQUAL(Permission::ReadWrite, property->permission);
  TEST_ASSERT_EQUAL_UINT8(1, property->memberCount);
  TEST_ASSERT_TRUE(fabs(property->members[0].numberValue - 1.5) < 0.0001);
  TEST_ASSERT_TRUE(fabs(property->members[0].maxValue - 3600) < 0.0001);
}

void test_parses_switch_rule_and_writes_complete_vector() {
  feedChunks("<defSwitchVector device=\"Mount &amp; Test\" name=\"TELESCOPE_SLEW_RATE\" "
             "state=\"Ok\" perm=\"rw\" rule=\"OneOfMany\">"
             "<defSwitch name=\"1x\">Off</defSwitch><defSwitch name=\"4x\">On</defSwitch>"
             "</defSwitchVector>", 3);
  const Property* property = cache.findProperty("Mount & Test", "TELESCOPE_SLEW_RATE");
  TEST_ASSERT_NOT_NULL(property);
  TEST_ASSERT_EQUAL(SwitchRule::OneOfMany, property->switchRule);
  char output[512];
  TEST_ASSERT_NOT_EQUAL(
      0, Writer::buildSwitchVector(output, sizeof(output), *property, property->members[0].name));
  TEST_ASSERT_EQUAL_STRING(
      "<newSwitchVector device=\"Mount &amp; Test\" name=\"TELESCOPE_SLEW_RATE\">"
      "<oneSwitch name=\"1x\">On</oneSwitch><oneSwitch name=\"4x\">Off</oneSwitch>"
      "</newSwitchVector>\n",
      output);
}

void test_trims_pretty_printed_switch_and_text_values() {
  feedChunks("<defSwitchVector device=\"Mount\" name=\"CONNECTION\">"
             "<defSwitch name=\"CONNECT\">\nOn\n    </defSwitch>"
             "<defSwitch name=\"DISCONNECT\">\nOff\n    </defSwitch></defSwitchVector>"
             "<defTextVector device=\"Mount\" name=\"TIME_UTC\">"
             "<defText name=\"UTC\">\n2026-06-13T10:24:04\n    </defText></defTextVector>", 4);
  const Property* connection = cache.findProperty("Mount", "CONNECTION");
  TEST_ASSERT_TRUE(connection->members[0].active);
  TEST_ASSERT_FALSE(connection->members[1].active);
  const Property* utc = cache.findProperty("Mount", "TIME_UTC");
  TEST_ASSERT_EQUAL_STRING("2026-06-13T10:24:04", utc->members[0].text);
}

void test_writes_all_off_motion_stop_and_detects_small_buffer() {
  feedChunks("<defSwitchVector device=\"Mount\" name=\"TELESCOPE_MOTION_NS\" rule=\"AtMostOne\">"
             "<defSwitch name=\"MOTION_NORTH\">Off</defSwitch>"
             "<defSwitch name=\"MOTION_SOUTH\">Off</defSwitch></defSwitchVector>", 8);
  const Property* property = cache.findProperty("Mount", "TELESCOPE_MOTION_NS");
  char output[512];
  TEST_ASSERT_NOT_EQUAL(0, Writer::buildSwitchVector(output, sizeof(output), *property, nullptr));
  TEST_ASSERT_NOT_NULL(strstr(output, "<oneSwitch name=\"MOTION_NORTH\">Off</oneSwitch>"));
  TEST_ASSERT_NOT_NULL(strstr(output, "<oneSwitch name=\"MOTION_SOUTH\">Off</oneSwitch>"));
  char tooSmall[10];
  TEST_ASSERT_EQUAL(0, Writer::buildSwitchVector(tooSmall, sizeof(tooSmall), *property, nullptr));
}

void test_writes_abort_using_discovered_member() {
  feedChunks("<defSwitchVector device=\"Mount\" name=\"TELESCOPE_ABORT_MOTION\" perm=\"rw\" "
             "rule=\"AtMostOne\"><defSwitch name=\"ABORT\">Off</defSwitch>"
             "</defSwitchVector>", 6);
  const Property* property = cache.findProperty("Mount", "TELESCOPE_ABORT_MOTION");
  char output[256];
  TEST_ASSERT_NOT_EQUAL(
      0, Writer::buildSwitchVector(output, sizeof(output), *property, property->members[0].name));
  TEST_ASSERT_NOT_NULL(strstr(output, "<oneSwitch name=\"ABORT\">On</oneSwitch>"));
}

void test_writes_number_vector() {
  feedChunks("<defNumberVector device=\"CCD &amp; Test\" name=\"CCD_EXPOSURE\" perm=\"rw\">"
             "<defNumber name=\"CCD_EXPOSURE_VALUE\">1</defNumber></defNumberVector>", 7);
  const Property* property = cache.findProperty("CCD & Test", "CCD_EXPOSURE");
  char output[256];
  TEST_ASSERT_NOT_EQUAL(0, Writer::buildNumberVector(output, sizeof(output), *property,
                                                    property->members[0].name, 2.5));
  TEST_ASSERT_EQUAL_STRING(
      "<newNumberVector device=\"CCD &amp; Test\" name=\"CCD_EXPOSURE\">"
      "<oneNumber name=\"CCD_EXPOSURE_VALUE\">2.5</oneNumber></newNumberVector>\n",
      output);
}

void test_writes_alert_number_vector() {
  feedChunks("<defNumberVector device=\"Camera\" name=\"CCD_EXPOSURE\" state=\"Alert\" perm=\"rw\">"
             "<defNumber name=\"CCD_EXPOSURE_VALUE\">1</defNumber></defNumberVector>", 7);
  const Property* property = cache.findProperty("Camera", "CCD_EXPOSURE");
  char output[256];
  TEST_ASSERT_EQUAL(State::Alert, property->state);
  TEST_ASSERT_NOT_EQUAL(
      0, Writer::buildNumberVector(output, sizeof(output), *property, "CCD_EXPOSURE_VALUE", 2));
}

void test_writes_complete_location_and_time_vectors() {
  feedChunks("<defNumberVector device=\"Mount &amp; Scope\" name=\"GEOGRAPHIC_COORD\" perm=\"rw\">"
             "<defNumber name=\"LAT\">0</defNumber><defNumber name=\"LONG\">0</defNumber>"
             "<defNumber name=\"ELEV\">123</defNumber></defNumberVector>"
             "<defTextVector device=\"Mount &amp; Scope\" name=\"TIME_UTC\" perm=\"rw\">"
             "<defText name=\"UTC\"></defText><defText name=\"OFFSET\"></defText>"
             "</defTextVector>", 9);
  const Property* location = cache.findProperty("Mount & Scope", "GEOGRAPHIC_COORD");
  const NumberValue locationValues[] = {{"LAT", 41.5}, {"LONG", 357.75}, {"ELEV", 123}};
  char output[512];
  TEST_ASSERT_NOT_EQUAL(
      0, Writer::buildNumberVector(output, sizeof(output), *location, locationValues, 3));
  TEST_ASSERT_EQUAL_STRING(
      "<newNumberVector device=\"Mount &amp; Scope\" name=\"GEOGRAPHIC_COORD\">"
      "<oneNumber name=\"LAT\">41.5</oneNumber><oneNumber name=\"LONG\">357.75</oneNumber>"
      "<oneNumber name=\"ELEV\">123</oneNumber></newNumberVector>\n",
      output);

  const Property* time = cache.findProperty("Mount & Scope", "TIME_UTC");
  const TextValue timeValues[] = {{"UTC", "2026-06-13T10:24:04"}, {"OFFSET", "0<&"}};
  TEST_ASSERT_NOT_EQUAL(0, Writer::buildTextVector(output, sizeof(output), *time, timeValues, 2));
  TEST_ASSERT_EQUAL_STRING(
      "<newTextVector device=\"Mount &amp; Scope\" name=\"TIME_UTC\">"
      "<oneText name=\"UTC\">2026-06-13T10:24:04</oneText>"
      "<oneText name=\"OFFSET\">0&lt;&amp;</oneText></newTextVector>\n",
      output);
  char tooSmall[20];
  TEST_ASSERT_EQUAL(0, Writer::buildTextVector(tooSmall, sizeof(tooSmall), *time, timeValues, 2));
}

void test_converts_indi_longitude_ranges() {
  TEST_ASSERT_TRUE(fabs(mount::longitudeToIndi(-2.25) - 357.75) < 0.000001);
  TEST_ASSERT_TRUE(fabs(mount::longitudeToIndi(362.25) - 2.25) < 0.000001);
  TEST_ASSERT_TRUE(fabs(mount::longitudeFromIndi(357.75) + 2.25) < 0.000001);
  TEST_ASSERT_TRUE(fabs(mount::longitudeFromIndi(180) - 180) < 0.000001);
}

void test_writes_single_switch_member_for_large_vectors() {
  feedChunks("<defSwitchVector device=\"Camera\" name=\"CCD_ISO\" perm=\"rw\">"
             "<defSwitch name=\"ISO4\">On</defSwitch><defSwitch name=\"ISO5\">Off</defSwitch>"
             "</defSwitchVector>", 6);
  const Property* property = cache.findProperty("Camera", "CCD_ISO");
  char output[256];
  TEST_ASSERT_NOT_EQUAL(
      0, Writer::buildSwitchMember(output, sizeof(output), *property, "ISO5", true));
  TEST_ASSERT_EQUAL_STRING(
      "<newSwitchVector device=\"Camera\" name=\"CCD_ISO\">"
      "<oneSwitch name=\"ISO5\">On</oneSwitch></newSwitchVector>\n",
      output);
}

void test_camera_model_detects_driver_and_controls() {
  feedChunks("<defTextVector device=\"Camera\" name=\"DRIVER_INFO\">"
             "<defText name=\"DRIVER_NAME\">Generic CCD</defText></defTextVector>"
             "<defNumberVector device=\"Camera\" name=\"CCD_EXPOSURE\">"
             "<defNumber name=\"CCD_EXPOSURE_VALUE\">5</defNumber></defNumberVector>"
             "<defSwitchVector device=\"Camera\" name=\"CCD_ISO\">"
             "<defSwitch name=\"ISO100\">Off</defSwitch><defSwitch name=\"ISO400\">On</defSwitch>"
             "</defSwitchVector>", 5);
  camera::Model model(cache, "Camera");
  TEST_ASSERT_TRUE(model.isCamera());
  TEST_ASSERT_EQUAL_STRING("CCD_EXPOSURE_VALUE", model.exposureMember()->name);
  TEST_ASSERT_EQUAL_STRING("ISO400", model.activeIso()->name);
  TEST_ASSERT_EQUAL_INT(1, model.activeIsoIndex());
}

void test_camera_model_retains_large_switch_vectors() {
  char xml[180];
  for (size_t i = 0; i < 89; ++i) {
    snprintf(xml, sizeof(xml),
             "<defSwitchVector device=\"Camera\" name=\"CCD_ISO\"><defSwitch name=\"ISO%u\" "
             "label=\"%u\">%s</defSwitch></defSwitchVector>",
             static_cast<unsigned>(i), static_cast<unsigned>(i * 100), i == 42 ? "On" : "Off");
    feedChunks(xml, 11);
  }
  camera::Model model(cache, "Camera");
  TEST_ASSERT_EQUAL_UINT(89, model.isoCount());
  TEST_ASSERT_EQUAL_INT(42, model.activeIsoIndex());
  TEST_ASSERT_EQUAL_STRING("ISO42", model.activeIso()->name);
  TEST_ASSERT_EQUAL_STRING("8800", model.iso(88)->label);
}

void test_mount_model_detects_capabilities_and_active_rate() {
  feedChunks("<defNumberVector device=\"Mount\" name=\"EQUATORIAL_EOD_COORD\">"
             "<defNumber name=\"RA\">12.5</defNumber><defNumber name=\"DEC\">-20.25</defNumber>"
             "</defNumberVector><defSwitchVector device=\"Mount\" name=\"TELESCOPE_SLEW_RATE\">"
             "<defSwitch name=\"Slow\">Off</defSwitch><defSwitch name=\"Fast\">On</defSwitch>"
             "</defSwitchVector>", 5);
  mount::Model model(cache, "Mount");
  TEST_ASSERT_TRUE(model.isMount());
  TEST_ASSERT_TRUE(fabs(model.member("EQUATORIAL_EOD_COORD", "RA")->numberValue - 12.5) < 0.0001);
  TEST_ASSERT_EQUAL_INT(1, model.activeSlewRateIndex());
  TEST_ASSERT_EQUAL_STRING("Fast", model.activeMember("TELESCOPE_SLEW_RATE")->name);
}

void test_mount_model_detects_disconnected_standard_telescope() {
  feedChunks("<defSwitchVector device=\"Celestron GPS\" name=\"CONNECTION\">"
             "<defSwitch name=\"CONNECT\">Off</defSwitch>"
             "<defSwitch name=\"DISCONNECT\">On</defSwitch></defSwitchVector>"
             "<defNumberVector device=\"Celestron GPS\" name=\"TELESCOPE_INFO\">"
             "<defNumber name=\"TELESCOPE_APERTURE\">0</defNumber></defNumberVector>", 7);
  mount::Model model(cache, "Celestron GPS");
  TEST_ASSERT_TRUE(model.isMount());
  TEST_ASSERT_EQUAL_STRING("DISCONNECT", model.activeMember("CONNECTION")->name);
}

void test_computes_horizontal_coordinates() {
  double altitude = 0;
  double azimuth = 0;
  TEST_ASSERT_TRUE(mount::equatorialToHorizontal(0, 0, 0, 0, "2000-01-01T12:00:00",
                                                altitude, azimuth));
  TEST_ASSERT_TRUE(altitude > 9 && altitude < 11);
  TEST_ASSERT_TRUE(azimuth > 89 && azimuth < 91);
}

void test_advances_utc_across_date_boundaries() {
  char output[32];
  TEST_ASSERT_TRUE(mount::addUtcSeconds("2026-12-31T23:59:59", 2, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("2027-01-01T00:00:01", output);
  TEST_ASSERT_TRUE(mount::addUtcSeconds("2028-02-28T23:59:59", 2, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("2028-02-29T00:00:01", output);
  TEST_ASSERT_FALSE(mount::addUtcSeconds("invalid", 1, output, sizeof(output)));
}

void test_validates_runtime_configuration() {
  app::RuntimeConfig config{};
  strcpy(config.ssid, "Observatory");
  strcpy(config.host, "indi.local");
  config.port = 7624;
  TEST_ASSERT_TRUE(app::validWifi(config));
  TEST_ASSERT_TRUE(app::validServer(config));
  strcpy(config.host, "bad host");
  TEST_ASSERT_FALSE(app::validServer(config));
  strcpy(config.host, "192.168.1.14");
  config.port = 0;
  TEST_ASSERT_FALSE(app::validServer(config));
}

void test_gnss_parses_valid_rmc_and_gga() {
  gnss::State state;
  const char* rmc =
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n";
  for (const char* value = rmc; *value; ++value) state.feed(*value, 1000, 9600);
  const char* gga =
      "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
  for (const char* value = gga; *value; ++value) state.feed(*value, 1200, 9600);
  const gnss::Snapshot snapshot = state.snapshot(1500);
  TEST_ASSERT_TRUE(snapshot.present);
  TEST_ASSERT_TRUE(snapshot.fixValid);
  TEST_ASSERT_TRUE(snapshot.dateTimeValid);
  TEST_ASSERT_TRUE(snapshot.locationValid);
  TEST_ASSERT_TRUE(snapshot.hdopValid);
  TEST_ASSERT_TRUE(snapshot.altitudeValid);
  TEST_ASSERT_EQUAL_UINT16(1994, snapshot.year);
  TEST_ASSERT_EQUAL_UINT8(8, snapshot.satellites);
  TEST_ASSERT_TRUE(fabs(snapshot.latitude - 48.1173) < 0.0001);
  TEST_ASSERT_TRUE(fabs(snapshot.longitude - 11.5166667) < 0.0001);
  TEST_ASSERT_TRUE(fabs(snapshot.hdop - 0.9) < 0.0001);
  TEST_ASSERT_TRUE(fabs(snapshot.altitudeMeters - 545.4) < 0.0001);
}

void test_gnss_requires_valid_checksum_and_times_out_presence() {
  gnss::State state;
  const char* invalid =
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*00\r\n";
  for (const char* value = invalid; *value; ++value) state.feed(*value, 1000, 9600);
  TEST_ASSERT_FALSE(state.snapshot(1000).present);
  const char* valid =
      "$GPRMC,123519,V,,,,,,,230394,,,N*51\r\n";
  for (const char* value = valid; *value; ++value) state.feed(*value, 2000, 38400);
  TEST_ASSERT_TRUE(state.snapshot(2000).present);
  const gnss::Snapshot stale = state.snapshot(2000 + gnss::kPresenceTimeoutMs + 1);
  TEST_ASSERT_FALSE(stale.present);
  TEST_ASSERT_EQUAL_UINT32(gnss::kPresenceTimeoutMs + 1, stale.messageAgeMs);
}

void test_gnss_detects_generic_sentence_and_firmware_response() {
  gnss::State state;
  const char* text = "$GPTXT,01,01,02,u-blox ag - www.u-blox.com*50\r\n";
  for (const char* value = text; *value; ++value) state.feed(*value, 1000, 115200);
  const gnss::Snapshot textSnapshot = state.snapshot(1000);
  TEST_ASSERT_TRUE(textSnapshot.present);
  TEST_ASSERT_EQUAL_STRING("u-blox ag - www.u-blox.com", textSnapshot.firmwareVersion);
  const char* firmware = "$PCAS06,VER,1.2.3*76\r\n";
  for (const char* value = firmware; *value; ++value) state.feed(*value, 1200, 115200);
  const gnss::Snapshot snapshot = state.snapshot(1200);
  TEST_ASSERT_EQUAL_UINT32(115200, snapshot.baudRate);
  TEST_ASSERT_EQUAL_STRING("VER,1.2.3", snapshot.firmwareVersion);
}

void test_gnss_tracks_explicit_gsa_fix_dimension() {
  gnss::State state;
  const char* twoD = "$GPGSA,A,2,04,05,,09,12,,,,,,,1.8,1.0,1.5*18\r\n";
  for (const char* value = twoD; *value; ++value) state.feed(*value, 1000, 115200);
  TEST_ASSERT_EQUAL(gnss::FixDimension::TwoD, state.snapshot(1000).fixDimension);
  const char* threeD = "$GPGSA,A,3,04,05,,09,12,,,,,,,1.8,1.0,1.5*19\r\n";
  for (const char* value = threeD; *value; ++value) state.feed(*value, 1200, 115200);
  const gnss::Snapshot snapshot = state.snapshot(1200);
  TEST_ASSERT_EQUAL(gnss::FixDimension::ThreeD, snapshot.fixDimension);
  TEST_ASSERT_TRUE(snapshot.hdopValid);
  TEST_ASSERT_TRUE(fabs(snapshot.hdop - 1.0) < 0.0001);
}

void test_updates_existing_property_and_decodes_entities() {
  const char* xml =
      "<defTextVector device=\"Mount &amp; Camera\" name=\"INFO\" state=\"Ok\" perm=\"ro\">"
      "<defText name=\"MODEL\">Old</defText></defTextVector>"
      "<setTextVector device=\"Mount &amp; Camera\" name=\"INFO\" state=\"Busy\">"
      "<oneText name=\"MODEL\">A&amp;B &lt;test&gt;</oneText></setTextVector>";
  feedChunks(xml, 7);

  const Property* property = cache.findProperty("Mount & Camera", "INFO");
  TEST_ASSERT_NOT_NULL(property);
  TEST_ASSERT_EQUAL(State::Busy, property->state);
  TEST_ASSERT_EQUAL_STRING("A&B <test>", property->members[0].text);
}

void test_skips_blob_payload_and_continues() {
  char blob[4096];
  memset(blob, 'A', sizeof(blob) - 1);
  blob[sizeof(blob) - 1] = '\0';
  char xml[4600];
  snprintf(xml, sizeof(xml),
           "<defBLOBVector device=\"CCD\" name=\"CCD1\" state=\"Idle\" perm=\"ro\">"
           "<defBLOB name=\"FRAME\"/></defBLOBVector>"
           "<setBLOBVector device=\"CCD\" name=\"CCD1\" state=\"Ok\">"
           "<oneBLOB name=\"FRAME\">%s</oneBLOB></setBLOBVector>"
           "<defSwitchVector device=\"Mount\" name=\"CONNECTION\" state=\"Idle\" perm=\"rw\">"
           "<defSwitch name=\"CONNECT\">Off</defSwitch></defSwitchVector>",
           blob);
  feedChunks(xml, 13);

  TEST_ASSERT_NOT_NULL(cache.findProperty("CCD", "CCD1"));
  TEST_ASSERT_NOT_NULL(cache.findProperty("Mount", "CONNECTION"));
}

void test_deletes_property() {
  feedChunks("<defTextVector device=\"D\" name=\"P\"><defText name=\"M\">x</defText>"
             "</defTextVector><delProperty device=\"D\" name=\"P\"/>", 5);
  TEST_ASSERT_NULL(cache.findProperty("D", "P"));
  TEST_ASSERT_EQUAL_UINT(0, cache.deviceCount());
}

void test_filters_properties_by_device() {
  feedChunks("<defTextVector device=\"A\" name=\"ONE\"/>"
             "<defTextVector device=\"B\" name=\"TWO\"/>"
             "<defTextVector device=\"A\" name=\"THREE\"/>", 9);
  TEST_ASSERT_EQUAL_UINT(2, cache.propertyCountForDevice("A"));
  TEST_ASSERT_EQUAL_STRING("ONE", cache.propertyForDevice("A", 0)->name);
  TEST_ASSERT_EQUAL_STRING("THREE", cache.propertyForDevice("A", 1)->name);
  TEST_ASSERT_NULL(cache.propertyForDevice("A", 2));
}

void test_capacity_overflow_is_reported() {
  char xml[200];
  for (size_t i = 0; i < kMaxDevices + 1; ++i) {
    snprintf(xml, sizeof(xml), "<defTextVector device=\"D%u\" name=\"P\"><defText name=\"M\">x"
                               "</defText></defTextVector>", static_cast<unsigned>(i));
    feedChunks(xml, 11);
  }
  TEST_ASSERT_TRUE(protocol.overflowed());
  TEST_ASSERT_EQUAL_UINT(kMaxDevices, cache.deviceCount());
}

void test_priority_control_property_displaces_nonessential_property() {
  char xml[200];
  for (size_t i = 0; i < kMaxProperties; ++i) {
    snprintf(xml, sizeof(xml), "<defTextVector device=\"D\" name=\"OTHER_%u\"/>",
             static_cast<unsigned>(i));
    feedChunks(xml, 13);
  }
  feedChunks("<defSwitchVector device=\"D\" name=\"TELESCOPE_SLEW_RATE\">"
             "<defSwitch name=\"1x\">Off</defSwitch><defSwitch name=\"9x\">On</defSwitch>"
             "</defSwitchVector>", 7);
  TEST_ASSERT_NOT_NULL(cache.findProperty("D", "TELESCOPE_SLEW_RATE"));
  TEST_ASSERT_EQUAL_UINT(kMaxProperties, cache.propertyCount());
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_definitions_across_single_byte_chunks);
  RUN_TEST(test_parses_switch_rule_and_writes_complete_vector);
  RUN_TEST(test_trims_pretty_printed_switch_and_text_values);
  RUN_TEST(test_writes_all_off_motion_stop_and_detects_small_buffer);
  RUN_TEST(test_writes_abort_using_discovered_member);
  RUN_TEST(test_writes_number_vector);
  RUN_TEST(test_writes_alert_number_vector);
  RUN_TEST(test_writes_complete_location_and_time_vectors);
  RUN_TEST(test_converts_indi_longitude_ranges);
  RUN_TEST(test_writes_single_switch_member_for_large_vectors);
  RUN_TEST(test_camera_model_detects_driver_and_controls);
  RUN_TEST(test_camera_model_retains_large_switch_vectors);
  RUN_TEST(test_mount_model_detects_capabilities_and_active_rate);
  RUN_TEST(test_mount_model_detects_disconnected_standard_telescope);
  RUN_TEST(test_computes_horizontal_coordinates);
  RUN_TEST(test_advances_utc_across_date_boundaries);
  RUN_TEST(test_validates_runtime_configuration);
  RUN_TEST(test_gnss_parses_valid_rmc_and_gga);
  RUN_TEST(test_gnss_requires_valid_checksum_and_times_out_presence);
  RUN_TEST(test_gnss_detects_generic_sentence_and_firmware_response);
  RUN_TEST(test_gnss_tracks_explicit_gsa_fix_dimension);
  RUN_TEST(test_updates_existing_property_and_decodes_entities);
  RUN_TEST(test_skips_blob_payload_and_continues);
  RUN_TEST(test_deletes_property);
  RUN_TEST(test_filters_properties_by_device);
  RUN_TEST(test_capacity_overflow_is_reported);
  RUN_TEST(test_priority_control_property_displaces_nonessential_property);
  return UNITY_END();
}
