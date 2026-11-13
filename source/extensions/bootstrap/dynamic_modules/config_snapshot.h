#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/server/config_tracker.h"
#include "envoy/upstream/cluster_manager.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

/**
 * A snapshot of the selected configuration providers.
 */
class ConfigSnapshot {
public:
  /**
   * Collects and redacts the selected providers. Must run on the server's main thread.
   */
  static absl::StatusOr<std::unique_ptr<ConfigSnapshot>>
  collect(const Server::ConfigTracker& tracker, const Upstream::ClusterManager& cluster_manager,
          absl::Span<const absl::string_view> providers);

  /**
   * Encodes the snapshot as ConfigDump JSON. Can run on any thread.
   */
  absl::StatusOr<std::string> serialize() const;

private:
  std::vector<ProtobufTypes::MessagePtr> messages_;
};

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
