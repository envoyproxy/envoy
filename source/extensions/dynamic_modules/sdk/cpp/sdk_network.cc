#include "sdk_network.h"

#include <format>
#include <iostream>

namespace Envoy {
namespace DynamicModules {

NetworkBuffer::~NetworkBuffer() = default;

NetworkFilterHandle::~NetworkFilterHandle() = default;

NetworkFilterConfigHandle::~NetworkFilterConfigHandle() = default;

NetworkFilter::~NetworkFilter() = default;

NetworkFilterFactory::~NetworkFilterFactory() = default;

NetworkFilterConfigFactory::~NetworkFilterConfigFactory() = default;

NetworkFilterConfigFactoryRegister::NetworkFilterConfigFactoryRegister(
    std::string_view name, NetworkFilterConfigFactoryPtr factory)
    : name_(name) {
  auto r = NetworkFilterConfigFactoryRegistry::getMutableRegistry().emplace(std::string_view(name_),
                                                                            std::move(factory));
  if (!r.second) {
    std::string error_msg = std::format("Factory with the same name {} already registered", name_);
    std::cerr << error_msg << std::endl;
    assert((void("Duplicate factory registration"), r.second));
  }
}

NetworkFilterConfigFactoryRegister::~NetworkFilterConfigFactoryRegister() {
  NetworkFilterConfigFactoryRegistry::getMutableRegistry().erase(name_);
}

std::map<std::string_view, NetworkFilterConfigFactoryPtr>&
NetworkFilterConfigFactoryRegistry::getMutableRegistry() {
  static std::map<std::string_view, NetworkFilterConfigFactoryPtr> registry;
  return registry;
}

const std::map<std::string_view, NetworkFilterConfigFactoryPtr>&
NetworkFilterConfigFactoryRegistry::getRegistry() {
  return getMutableRegistry();
};

} // namespace DynamicModules
} // namespace Envoy
