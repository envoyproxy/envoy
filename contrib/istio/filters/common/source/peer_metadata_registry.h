#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/server/factory_context.h"

#include "absl/strings/string_view.h"

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

  virtual void setValue(absl::string_view key, const std::string& value) PURE;

  virtual std::optional<std::string> getValue(absl::string_view key) const PURE;

  virtual void removeValue(absl::string_view key) PURE;
};

using PeerMetadataRegistrySharedPtr = std::shared_ptr<PeerMetadataRegistry>;

// Returns the process-wide PeerMetadataRegistry singleton, creating it if
// necessary. The registry is pinned in the singleton manager.
PeerMetadataRegistrySharedPtr getRegistry(Server::Configuration::ServerFactoryContext& context);

constexpr absl::string_view ConnectionIdFilterStateKey =
    "envoy.peer_metadata.downstream_connection_id";

} // namespace PeerMetadataShared
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
