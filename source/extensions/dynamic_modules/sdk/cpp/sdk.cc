#include "sdk.h"

namespace Envoy {
namespace DynamicModules {

HttpFilterConfigFactory::~HttpFilterConfigFactory() = default;

HttpFilterFactory::~HttpFilterFactory() = default;

HttpFilter::~HttpFilter() = default;

BodyBuffer::~BodyBuffer() = default;

HeaderMap::~HeaderMap() = default;

HttpCalloutCallback::~HttpCalloutCallback() = default;

HttpStreamCallback::~HttpStreamCallback() = default;

RouteSpecificConfig::~RouteSpecificConfig() = default;

Scheduler::~Scheduler() = default;

DownstreamWatermarkCallbacks::~DownstreamWatermarkCallbacks() = default;

HttpFilterHandle::~HttpFilterHandle() = default;

HttpFilterConfigHandle::~HttpFilterConfigHandle() = default;

HttpFilterConfigFactoryRegister::HttpFilterConfigFactoryRegister(absl::string_view name,
                                                                 HttpFilterConfigFactoryPtr factory)
    : name_(name) {
  auto r = HttpFilterConfigFactoryRegistry::getMutableRegistry().emplace(name_, std::move(factory));
  if (!r.second) {
    std::string error_msg = std::format("Factory with the same name {} already registered", name_);
    std::cerr << error_msg << std::endl;
    assert((void("Duplicate factory registration"), r.second));
  }
}

HttpFilterConfigFactoryRegister::~HttpFilterConfigFactoryRegister() {
  HttpFilterConfigFactoryRegistry::getMutableRegistry().erase(name_);
}

absl::flat_hash_map<std::string, HttpFilterConfigFactoryPtr>&
HttpFilterConfigFactoryRegistry::getMutableRegistry() {
  static absl::flat_hash_map<std::string, HttpFilterConfigFactoryPtr> registry;
  return registry;
}

const absl::flat_hash_map<std::string, HttpFilterConfigFactoryPtr>&
HttpFilterConfigFactoryRegistry::getRegistry() {
  return getMutableRegistry();
};

} // namespace DynamicModules
} // namespace Envoy
