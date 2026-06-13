#include "ConfigStore.h"

#include <Preferences.h>

namespace app {

bool ConfigStore::load(RuntimeConfig& config) {
  Preferences preferences;
  if (!preferences.begin("indi-control", true)) return false;
  const bool configured = preferences.getBool("configured", false);
  if (configured) {
    preferences.getString("ssid", config.ssid, sizeof(config.ssid));
    preferences.getString("password", config.password, sizeof(config.password));
    preferences.getString("host", config.host, sizeof(config.host));
    config.port = preferences.getUShort("port", 7624);
  }
  preferences.end();
  return configured && validWifi(config) && validServer(config);
}

bool ConfigStore::save(const RuntimeConfig& config) {
  if (!validWifi(config) || !validServer(config)) return false;
  Preferences preferences;
  if (!preferences.begin("indi-control", false)) return false;
  const bool ok = preferences.putString("ssid", config.ssid) &&
                  preferences.putString("password", config.password) &&
                  preferences.putString("host", config.host) &&
                  preferences.putUShort("port", config.port) &&
                  preferences.putBool("configured", true);
  preferences.end();
  return ok;
}

bool ConfigStore::clear() {
  Preferences preferences;
  if (!preferences.begin("indi-control", false)) return false;
  const bool ok = preferences.clear();
  preferences.end();
  return ok;
}

}  // namespace app

