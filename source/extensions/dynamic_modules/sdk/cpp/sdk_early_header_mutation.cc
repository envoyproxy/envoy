#include "sdk_early_header_mutation.h"

#include <cassert>
#include <format>
#include <iostream>

namespace Envoy {
namespace DynamicModules {

EarlyHeaderMutationHandle::~EarlyHeaderMutationHandle() = default;

EarlyHeaderMutationConfigHandle::~EarlyHeaderMutationConfigHandle() = default;

EarlyHeaderMutation::~EarlyHeaderMutation() = default;

EarlyHeaderMutationConfigFactory::~EarlyHeaderMutationConfigFactory() = default;

EarlyHeaderMutationConfigFactoryRegister::EarlyHeaderMutationConfigFactoryRegister(
    std::string_view name, EarlyHeaderMutationConfigFactoryPtr factory)
    : name_(name) {
  auto r = EarlyHeaderMutationConfigFactoryRegistry::getMutableRegistry().emplace(
      std::string_view(name_), std::move(factory));
  if (!r.second) {
    const std::string error_msg =
        std::format("Factory with the same name {} already registered", name_);
    std::cerr << error_msg << std::endl;
    assert((void("Duplicate factory registration"), r.second));
  }
}

EarlyHeaderMutationConfigFactoryRegister::~EarlyHeaderMutationConfigFactoryRegister() {
  EarlyHeaderMutationConfigFactoryRegistry::getMutableRegistry().erase(name_);
}

std::map<std::string_view, EarlyHeaderMutationConfigFactoryPtr>&
EarlyHeaderMutationConfigFactoryRegistry::getMutableRegistry() {
  static std::map<std::string_view, EarlyHeaderMutationConfigFactoryPtr> registry;
  return registry;
}

const std::map<std::string_view, EarlyHeaderMutationConfigFactoryPtr>&
EarlyHeaderMutationConfigFactoryRegistry::getRegistry() {
  return getMutableRegistry();
}

} // namespace DynamicModules
} // namespace Envoy
