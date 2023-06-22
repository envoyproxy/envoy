#include "source/common/common/inline_map_registry.h"

namespace Envoy {

uint64_t InlineMapRegistry::generateRegistryId() {
  static std::atomic<uint64_t> next_id{0};
  return next_id++;
}

InlineMapRegistry::InlineKey InlineMapRegistry::registerInlineKey(absl::string_view key) {
  RELEASE_ASSERT(!finalized_, "Cannot register inline key after finalized()");

  // If the key is already registered, return the existing handle.
  if (auto it = registration_map_.find(key); it != registration_map_.end()) {
    // It is safe to reference the key here because the key is stored in the node hash set.
    return {registry_id_, it->second, it->first};
  }

  // If the key is not registered, create a new handle for it.

  const uint64_t inline_id = registration_map_.size();

  // Insert the key to the list and keep the reference of the key in the inline handle and
  // registration map.
  auto result = registration_set_.emplace(key);

  RELEASE_ASSERT(result.second,
                 "The key should not be registered repeatedly and this should not happen.");

  // It is safe to reference the key here because the key is stored in the node hash set.
  registration_map_.emplace(absl::string_view(*result.first), inline_id);
  return {registry_id_, inline_id, absl::string_view(*result.first)};
}

absl::optional<InlineMapRegistry::InlineKey>
InlineMapRegistry::getInlineKey(absl::string_view key) const {
  ASSERT(finalized_, "Cannot get inline handle before finalized()");

  if (auto it = registration_map_.find(key); it != registration_map_.end()) {
    // It is safe to reference the key here because the key is stored in the node hash set.
    return InlineKey(registry_id_, it->second, it->first);
  }

  return absl::nullopt;
}

const InlineMapRegistry::RegistrationMap& InlineMapRegistry::registrationMap() const {
  ASSERT(finalized_, "Cannot fetch registration map before finalized()");
  return registration_map_;
}

uint64_t InlineMapRegistry::registrationNum() const {
  ASSERT(finalized_, "Cannot fetch registration map before finalized()");
  return registration_map_.size();
}

} // namespace Envoy
