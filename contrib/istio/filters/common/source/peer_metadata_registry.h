#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/server/factory_context.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace PeerMetadataShared {

// Process-wide singleton that exposes a per-worker-thread key/value store used
// to hand peer metadata information from the peer_metadata network filter on an
// internal listener (writer) to the peer_metadata upstream network filter on a
// cluster (reader).
//
// The backing storage is a ThreadLocal::TypedSlot, so each worker thread owns
// its own map and reads/writes are lock-free. This only works as a hand-off
// when the writing and reading filters run on the *same* worker thread (which
// is the case for internal listener connections), and when both sides agree on
// the key (the originating upstream connection ID).
class PeerMetadataRegistry {
public:
  virtual ~PeerMetadataRegistry() = default;

  // Store `value` under `key` in the current worker thread's store.
  virtual void setValue(uint64_t key, const std::string& value) PURE;

  // Look up `key` in the current worker thread's store. Returns absl::nullopt
  // if no value was stored (on this thread) under `key`.
  virtual absl::optional<std::string> getValue(uint64_t key) const PURE;

  // Remove the entry for `key` from the current worker thread's store.
  virtual void removeValue(uint64_t key) PURE;
};

using PeerMetadataRegistrySharedPtr = std::shared_ptr<PeerMetadataRegistry>;

// Returns the process-wide PeerMetadataRegistry singleton, creating it if
// necessary. The registry is pinned in the singleton manager.
PeerMetadataRegistrySharedPtr
getRegistry(Server::Configuration::ServerFactoryContext& context);

// Filter state key under which the originating upstream connection ID is stored.
constexpr absl::string_view ConnectionIdFilterStateKey =
    "envoy.peer_metadata.upstream_connection_id";

} // namespace PeerMetadataShared
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
