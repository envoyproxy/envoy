#include "sdk_listener.h"

#include <cassert>
#include <format>
#include <iostream>

namespace Envoy {
namespace DynamicModules {

ListenerFilterHandle::~ListenerFilterHandle() = default;

ListenerFilterConfigHandle::~ListenerFilterConfigHandle() = default;

ListenerFilter::~ListenerFilter() = default;

ListenerFilterFactory::~ListenerFilterFactory() = default;

ListenerFilterConfigFactory::~ListenerFilterConfigFactory() = default;

ListenerFilterConfigFactoryRegister::ListenerFilterConfigFactoryRegister(
    std::string_view name, ListenerFilterConfigFactoryPtr factory)
    : name_(name) {
  auto r = ListenerFilterConfigFactoryRegistry::getMutableRegistry().emplace(
      std::string_view(name_), std::move(factory));
  if (!r.second) {
    const std::string error_msg =
        std::format("Factory with the same name {} already registered", name_);
    std::cerr << error_msg << std::endl;
    assert((void("Duplicate factory registration"), r.second));
  }
}

ListenerFilterConfigFactoryRegister::~ListenerFilterConfigFactoryRegister() {
  ListenerFilterConfigFactoryRegistry::getMutableRegistry().erase(name_);
}

std::map<std::string_view, ListenerFilterConfigFactoryPtr>&
ListenerFilterConfigFactoryRegistry::getMutableRegistry() {
  static std::map<std::string_view, ListenerFilterConfigFactoryPtr> registry;
  return registry;
}

const std::map<std::string_view, ListenerFilterConfigFactoryPtr>&
ListenerFilterConfigFactoryRegistry::getRegistry() {
  return getMutableRegistry();
}

} // namespace DynamicModules
} // namespace Envoy
