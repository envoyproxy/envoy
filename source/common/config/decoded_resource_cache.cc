#include "source/common/config/decoded_resource_cache.h"

#include "envoy/service/discovery/v3/discovery.pb.h"

namespace Envoy {
namespace Config {

std::string DecodedResourceCache::keyOf(const Protobuf::Any& resource) {
  // The type URL is part of the key so identical bytes of different types never match.
  std::string key;
  key.reserve(resource.type_url().size() + 1 + resource.value().size());
  key.append(resource.type_url());
  key.push_back('\0');
  key.append(resource.value());
  return key;
}

absl::StatusOr<DecodedResourcePtr>
DecodedResourceCache::decode(OpaqueResourceDecoder& resource_decoder, const Protobuf::Any& resource,
                             const std::string& version, Protobuf::Arena& arena) {
  std::string key = keyOf(resource);
  if (auto it = current_.find(key); it != current_.end()) {
    // The same bytes twice in one update: reuse, the subscriber reports duplicates.
    ++hits_;
    return std::make_unique<CachedResource>(it->second, version);
  }
  if (auto node = previous_.extract(key); !node.empty()) {
    ++hits_;
    EntrySharedPtr entry = node.mapped();
    current_.insert(std::move(node));
    return std::make_unique<CachedResource>(std::move(entry), version);
  }

  ++misses_;
  absl::StatusOr<DecodedResourceImplPtr> decoded =
      DecodedResourceImpl::fromResource(resource_decoder, resource, version, arena);
  RETURN_IF_NOT_OK_REF(decoded.status());
  DecodedResourceImplPtr& impl = decoded.value();
  if (impl->hasResource()) {
    // The decoded message lives on this update's arena; keep a heap copy for later updates.
    auto entry = std::make_shared<Entry>();
    entry->message.reset(impl->resource().New());
    entry->message->CopyFrom(impl->resource());
    entry->name = impl->name();
    entry->aliases = impl->aliases();
    if (resource.Is<envoy::service::discovery::v3::Resource>()) {
      entry->own_version = impl->version();
    }
    entry->ttl = impl->ttl();
    if (impl->metadata().has_value()) {
      entry->metadata = *impl->metadata();
    }
    current_.emplace(std::move(key), entry);
    // Hand out the cached copy so every update sees the same message.
    return std::make_unique<CachedResource>(std::move(entry), version);
  }
  return DecodedResourcePtr(std::move(impl));
}

void DecodedResourceCache::endUpdate() {
  previous_ = std::move(current_);
  current_.clear();
}

} // namespace Config
} // namespace Envoy
