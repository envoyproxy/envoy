#pragma once

#include "envoy/admin/v3/reverse_xds.pb.h"
#include "envoy/server/listener_manager.h"

#include "source/extensions/config_subscription/grpc/bidirectional_grpc_mux.h"

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Server {

/**
 * Provider for listener readiness status resources.
 * This tracks listener lifecycle events and provides status information
 * for reverse xDS requests.
 */
class ListenerStatusProvider : public Config::ClientResourceProvider,
                              public Logger::Loggable<Logger::Id::main> {
public:
  ListenerStatusProvider() = default;

  // Config::ClientResourceProvider
  std::vector<Protobuf::Any> getResources(
      const std::vector<std::string>& resource_names) const override;
  
  std::string getTypeUrl() const override {
    return "type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus";
  }
  
  std::string getVersionInfo() const override;

  /**
   * Update the status of a listener.
   * These methods would typically be called by the listener manager.
   */
  void updateListenerStatus(const std::string& name, 
                           const envoy::admin::v3::ListenerReadinessStatus& status);
  void removeListener(const std::string& name);

  /**
   * Convenience methods for updating status with different states.
   */
  void onListenerAdded(const std::string& name);
  void onListenerReady(const std::string& name, const std::string& bound_address);
  void onListenerFailed(const std::string& name, const std::string& error);
  void onListenerDraining(const std::string& name);

private:
  void incrementVersion();
  envoy::admin::v3::ListenerReadinessStatus createStatus(
      const std::string& name,
      envoy::admin::v3::ListenerReadinessStatus::State state,
      const std::string& bound_address = "",
      const std::string& error_message = "") const;

  mutable absl::Mutex mutex_;
  absl::flat_hash_map<std::string, envoy::admin::v3::ListenerReadinessStatus> listeners_ 
      ABSL_GUARDED_BY(mutex_);
  
  mutable absl::Mutex version_mutex_;
  std::string version_info_ ABSL_GUARDED_BY(version_mutex_) = "1";
  uint64_t version_counter_ ABSL_GUARDED_BY(version_mutex_) = 1;
};

} // namespace Server
} // namespace Envoy 