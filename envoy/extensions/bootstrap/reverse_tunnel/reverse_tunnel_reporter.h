#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Interface for emitting reverse-tunnel lifecycle events.
 */
class ReverseTunnelReporter {
public:
  virtual ~ReverseTunnelReporter() = default;

  /**
   * Called after the Envoy server finishes initialization.
   */
  virtual void onServerInitialized() PURE;

  /**
   * Record that a reverse tunnel has been established.
   * @param node_id ID reported by the connecting node.
   * @param cluster_id cluster which the node belongs to.
   * @param tenant_id tenant identifier associated with the node.
   * @param initiation_time_ms epoch milliseconds when the tunnel was initiated at the downstream
   *        (DP) envoy. 0 means the timestamp was not provided (e.g. older DP envoys that don't
   *        send the initiation-time header).
   */
  virtual void reportConnectionEvent(absl::string_view node_id, absl::string_view cluster_id,
                                     absl::string_view tenant_id, int64_t initiation_time_ms) PURE;

  /**
   * Record that a reverse tunnel has been torn down.
   * @param node_id ID of the disconnecting node.
   * @param cluster_id cluster which the node belongs to.
   */
  virtual void reportDisconnectionEvent(absl::string_view node_id,
                                        absl::string_view cluster_id) PURE;
};

using ReverseTunnelReporterPtr = std::unique_ptr<ReverseTunnelReporter>;

/**
 * Factory for creating reverse-tunnel reporters.
 */
class ReverseTunnelReporterFactory : public Config::TypedFactory {
public:
  /**
   * Build a reporter instance from the supplied configuration.
   * @param context owning server factory context.
   * @param message typed reporter configuration; ownership is transferred to the callee.
   * @return unique ptr to the reporter instance.
   */
  virtual ReverseTunnelReporterPtr
  createReporter(Server::Configuration::ServerFactoryContext& context,
                 ProtobufTypes::MessagePtr message) PURE;

  std::string category() const override {
    return "envoy.extensions.reverse_tunnel.reporting_service";
  }
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
