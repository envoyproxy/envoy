#pragma once

#include <memory>
#include <string>

#include "envoy/server/instance.h"
#include "envoy/config/xds_manager.h"

#include "source/common/config/bidirectional_xds_config.h"
#include "source/extensions/config_subscription/grpc/bidirectional_grpc_mux.h"
#include "source/extensions/config_subscription/grpc/listener_status_provider.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Server {

/**
 * Manager for bidirectional xDS functionality.
 * Handles setup and integration of reverse xDS providers during server startup.
 */
class BidirectionalXdsManager : public Logger::Loggable<Logger::Id::main> {
public:
  BidirectionalXdsManager(Instance& server);

  /**
   * Initialize bidirectional xDS if enabled.
   * Called during server startup after xDS manager is available.
   */
  void initialize();

  /**
   * Notify about listener lifecycle events.
   * These will be forwarded to the listener status provider if enabled.
   */
  void onListenerAdded(const std::string& name);
  void onListenerReady(const std::string& name, const std::string& bound_address);
  void onListenerFailed(const std::string& name, const std::string& error);
  void onListenerDraining(const std::string& name);
  void onListenerRemoved(const std::string& name);

  /**
   * Update listener status from the current listener manager state.
   * This can be called periodically or after listener configuration updates.
   */
  void updateListenerStatusFromManager();

  /**
   * Check if bidirectional xDS is enabled and initialized.
   */
  bool isEnabled() const { return enabled_; }

private:
  /**
   * Setup listener status provider if configured.
   */
  void setupListenerStatusProvider();

  /**
   * Get bidirectional mux from ADS if available.
   */
  std::shared_ptr<Config::BidirectionalGrpcMuxImpl> getBidirectionalAdsMux();

  /**
   * Check if bidirectional xDS should be enabled based on configuration.
   */
  bool shouldEnable() const;

  Instance& server_;
  Config::BidirectionalXdsConfig config_;
  bool enabled_ = false;
  bool initialized_ = false;

  // Weak reference to the registered listener status provider
  // (ownership is transferred to the bidirectional mux)
  ListenerStatusProvider* listener_status_provider_ = nullptr;
};

} // namespace Server
} // namespace Envoy