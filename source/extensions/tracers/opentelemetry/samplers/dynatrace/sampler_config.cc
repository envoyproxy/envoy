#include <utility>

#include "source/common/json/json_loader.h"
#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/sampler_config_provider.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

bool SamplerConfig::parse(const std::string& json) {
  const auto result = Envoy::Json::Factory::loadFromStringNoThrow(json);
  if (result.ok()) {
    const auto& obj = result.value();
    if (obj->hasObject("rootSpansPerMinute")) {
      const auto value = obj->getInteger("rootSpansPerMinute", default_root_spans_per_minute_);
      root_spans_per_minute_.store(value);
      return true;
    }
  }
  // Didn't get a value, reset to default
  root_spans_per_minute_.store(default_root_spans_per_minute_);
  return false;
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
