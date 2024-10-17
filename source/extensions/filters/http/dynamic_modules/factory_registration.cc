#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/dynamic_modules/factory.h"

namespace Envoy {
namespace Server {
namespace Configuration {

REGISTER_FACTORY(DynamicModuleConfigFactory, NamedHttpFilterConfigFactory);

} // namespace Configuration
} // namespace Server
} // namespace Envoy
