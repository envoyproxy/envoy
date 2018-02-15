#include "exe/main_common.h"

#include <iostream>
#include <memory>

#include "common/common/compiler_requirements.h"
#include "common/event/libevent.h"
#include "common/network/utility.h"
#include "common/stats/stats_impl.h"

#include "server/config_validation/server.h"
#include "server/drain_manager_impl.h"
#include "server/hot_restart_nop_impl.h"
#include "server/options_impl.h"
#include "server/proto_descriptors.h"
#include "server/server.h"
#include "server/test_hooks.h"

#ifdef ENVOY_HOT_RESTART
#include "server/hot_restart_impl.h"
#endif

#include "ares.h"

namespace Envoy {

Server::DrainManagerPtr ProdComponentFactory::createDrainManager(Server::Instance& server) {
  // The global drain manager only triggers on listener modification, which effectively is
  // hot restart at the global level. The per-listener drain managers decide whether to
  // to include /healthcheck/fail status.
  return std::make_unique<Server::DrainManagerImpl>(server,
                                                    envoy::api::v2::Listener_DrainType_MODIFY_ONLY);
}

Runtime::LoaderPtr ProdComponentFactory::createRuntime(Server::Instance& server,
                                                       Server::Configuration::Initial& config) {
  return Server::InstanceUtil::createRuntime(server, config);
}

MainCommonBase::MainCommonBase(OptionsImpl& options, bool hot_restart) : options_(options) {
  ares_library_init(ARES_LIB_INIT_ALL);
  Event::Libevent::Global::initialize();
  RELEASE_ASSERT(Envoy::Server::validateProtoDescriptors());

  switch (options_.mode()) {
  case Server::Mode::Serve: {
#ifdef ENVOY_HOT_RESTART
    if (hot_restart) {
      restarter_.reset(new Server::HotRestartImpl(options_));
    }
#endif
    if (!hot_restart) {
      restarter_.reset(new Server::HotRestartNopImpl());
    }

    Stats::RawStatData::configure(options_);
    tls_.reset(new ThreadLocal::InstanceImpl);
    Thread::BasicLockable& log_lock = restarter_->logLock();
    Thread::BasicLockable& access_log_lock = restarter_->accessLogLock();
    auto local_address = Network::Utility::getLocalAddress(options_.localAddressIpVersion());
    Logger::Registry::initialize(options_.logLevel(), log_lock);

    stats_store_.reset(new Stats::ThreadLocalStoreImpl(restarter_->statsAllocator()));
    server_.reset(new Server::InstanceImpl(options_, local_address, default_test_hooks_,
                                           *restarter_, *stats_store_, access_log_lock,
                                           component_factory_, *tls_));
    break;
  }
  case Server::Mode::Validate:
    break;
  }
}

MainCommonBase::~MainCommonBase() { ares_library_cleanup(); }

bool MainCommonBase::run() {
  switch (options_.mode()) {
  case Server::Mode::Serve:
    server_->run();
    return true;
  case Server::Mode::Validate: {
    auto local_address = Network::Utility::getLocalAddress(options_.localAddressIpVersion());
    return Server::validateConfig(options_, local_address, component_factory_);
  }
  }
  NOT_REACHED;
}

MainCommon::MainCommon(int argc, char** argv, bool hot_restart)
    : options_(computeOptions(argc, argv, hot_restart)), base_(*options_, hot_restart) {}

std::unique_ptr<OptionsImpl> MainCommon::computeOptions(int argc, char** argv, bool hot_restart) {
  OptionsImpl::HotRestartVersionCb hot_restart_version_cb = [](uint64_t, uint64_t) {
    return "disabled";
  };

#ifdef ENVOY_HOT_RESTART
  if (hot_restart) {
    // Enabled by default, except on OS X. Control with "bazel --define=hot_restart=disabled"
    hot_restart_version_cb = [](uint64_t max_num_stats, uint64_t max_stat_name_len) {
      return Server::HotRestartImpl::hotRestartVersion(max_num_stats, max_stat_name_len);
    };
  }
#else
  // Hot-restart should not be specified if the support is not compiled in.
  RELEASE_ASSERT(!hot_restart);
#endif
  return std::make_unique<OptionsImpl>(argc, argv, hot_restart_version_cb, spdlog::level::info);
}

// Legacy implementation of main_common.
//
// TODO(jmarantz): Remove this when all callers are removed. At that time, MainCommonBase
// and MainCommon can be merged. The current theory is that only Google calls this.
int main_common(OptionsImpl& options) {
  try {
#if ENVOY_HOT_RESTART
    MainCommonBase main_common(options, true);
#else
    MainCommonBase main_common(options, false);
#endif
    return main_common.run() ? EXIT_SUCCESS : EXIT_FAILURE;
  } catch (EnvoyException& e) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

} // namespace Envoy
