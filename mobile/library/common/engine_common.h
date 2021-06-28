#pragma once

#include "envoy/event/timer.h"
#include "envoy/server/instance.h"

#include "source/common/event/real_time_system.h"
#include "source/exe/main_common.h"
#include "source/exe/platform_impl.h"
#include "source/server/listener_hooks.h"
#include "source/server/options_impl.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "source/common/signal/signal_action.h"
#include "source/exe/terminate_handler.h"
#endif

namespace Envoy {

/**
 * This class is used instead of Envoy::MainCommon to customize logic for the Envoy Mobile setting.
 * It largely leverages Envoy::MainCommonBase.
 */
class EngineCommon {
public:
  EngineCommon(int argc, const char* const* argv);
  bool run() { return base_.run(); }

  /**
   * @return a pointer to the server instance, or nullptr if initialized into
   *         validation mode.
   */
  Server::Instance* server() { return base_.server(); }

private:
#ifdef ENVOY_HANDLE_SIGNALS
  // TODO(junr03): build a derived Event::SignalAction that uses the Envoy Logger as the ostream.
  // https://github.com/envoyproxy/envoy-mobile/issues/1497.
  Envoy::SignalAction handle_sigs_;
  Envoy::TerminateHandler log_on_terminate_;
#endif

  Envoy::OptionsImpl options_;
  Event::RealTimeSystem real_time_system_; // NO_CHECK_FORMAT(real_time)
  DefaultListenerHooks default_listener_hooks_;
  ProdComponentFactory prod_component_factory_;
  MainCommonBase base_;
};

} // namespace Envoy
