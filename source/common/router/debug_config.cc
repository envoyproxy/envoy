#include "source/common/router/debug_config.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Router {

constexpr absl::string_view DebugConfigKey = "envoy.router.debug_config";

absl::string_view DebugConfig::key() { return DebugConfigKey; }

std::string DebugConfigFactory::name() const { return std::string(DebugConfigKey); }

std::unique_ptr<StreamInfo::FilterState::Object>
DebugConfigFactory::createFromBytes(absl::string_view data) const {
  return std::make_unique<DebugConfig>(data == "true");
}

REGISTER_FACTORY(DebugConfigFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace Router
} // namespace Envoy
