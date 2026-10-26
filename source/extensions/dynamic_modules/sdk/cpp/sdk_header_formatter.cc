#include "sdk_header_formatter.h"

#include <cassert>
#include <format>
#include <iostream>

namespace Envoy {
namespace DynamicModules {

HeaderFormatterConfigHandle::~HeaderFormatterConfigHandle() = default;

HeaderFormatterHandle::~HeaderFormatterHandle() = default;

HeaderFormatter::~HeaderFormatter() = default;

HeaderFormatterConfig::~HeaderFormatterConfig() = default;

HeaderFormatterConfigFactory::~HeaderFormatterConfigFactory() = default;

HeaderFormatterConfigFactoryRegister::HeaderFormatterConfigFactoryRegister(
    std::string_view name, HeaderFormatterConfigFactoryPtr factory)
    : name_(name) {
  auto r = HeaderFormatterConfigFactoryRegistry::getMutableRegistry().emplace(
      std::string_view(name_), std::move(factory));
  if (!r.second) {
    const std::string error_msg =
        std::format("Factory with the same name {} already registered", name_);
    std::cerr << error_msg << std::endl;
    assert((void("Duplicate factory registration"), r.second));
  }
}

HeaderFormatterConfigFactoryRegister::~HeaderFormatterConfigFactoryRegister() {
  HeaderFormatterConfigFactoryRegistry::getMutableRegistry().erase(name_);
}

std::map<std::string_view, HeaderFormatterConfigFactoryPtr>&
HeaderFormatterConfigFactoryRegistry::getMutableRegistry() {
  static std::map<std::string_view, HeaderFormatterConfigFactoryPtr> registry;
  return registry;
}

const std::map<std::string_view, HeaderFormatterConfigFactoryPtr>&
HeaderFormatterConfigFactoryRegistry::getRegistry() {
  return getMutableRegistry();
}

} // namespace DynamicModules
} // namespace Envoy
