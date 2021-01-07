#include "common/json/json_loader.h"

#include "common/json/json_internal.h"
#include "common/json/json_internal_legacy.h"
#include "common/runtime/runtime_features.h"

namespace Envoy {
namespace Json {

ObjectSharedPtr Factory::loadFromString(const std::string& json) {
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.remove_rapidjson")) {
    return Nlohmann::Factory::loadFromString(json);
  }
  return RapidJson::Factory::loadFromString(json);
}

} // namespace Json
} // namespace Envoy
