#include "extensions/common/tap/extension_config_base.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

/*
Plan:
Create TapConfigProviderManagerImpl that:
Has a function

ExtensionConfigBase::ExtensionConfigBase(){
case grpc:

// we might not even need manager................. as we don't have more than one name per tap filter!
  //
  TapSub tsb := new_subscribe(config_source, name, *this...)
  // save sub inside us
}

in TapSub::onConfgUpdate() {
  extension_config_.newTapConfig(theprotowejustgot, nullptr)
}

// edge case: filter is more than one filter chain (likely)
solution: implement manager :\


*/
SINGLETON_MANAGER_REGISTRATION(tap_config_provider_manager);


// This is my RdsRouteConfigProviderImpl
ExtensionConfigBase::ExtensionConfigBase(
    const envoy::config::common::tap::v2alpha::CommonExtensionConfig proto_config,
    TapConfigFactoryPtr&& config_factory, Server::Admin& admin,
    Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& main_thread_dispatcher)
    : proto_config_(proto_config), config_factory_(std::move(config_factory)),
      tls_slot_(tls.allocateSlot()) {
  tls_slot_->set([](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<TlsFilterConfig>();
  });

  switch (proto_config_.config_type_case()) {
  case envoy::config::common::tap::v2alpha::CommonExtensionConfig::kAdminConfig: {
    admin_handler_ = AdminHandler::getSingleton(admin, singleton_manager, main_thread_dispatcher);
    admin_handler_->registerConfig(*this, proto_config_.admin_config().config_id());
    ENVOY_LOG(debug, "initializing tap extension with admin endpoint (config_id={})",
              proto_config_.admin_config().config_id());
    break;
  }
  case envoy::config::common::tap::v2alpha::CommonExtensionConfig::kStaticConfig: {
    newTapConfig(envoy::service::tap::v2alpha::TapConfig(proto_config_.static_config()), nullptr);
    ENVOY_LOG(debug, "initializing tap extension with static config");
    break;
  }
  case envoy::config::common::tap::v2alpha::CommonExtensionConfig::kTdsConfig: {
/*
  std::shared_ptr<TapConfigProviderManager> tap_config_provider_manager =
      context.singletonManager().getTyped<Router::RouteConfigProviderManager>(
          SINGLETON_MANAGER_REGISTERED_NAME(tap_config_provider_manager), [&context] {
            
      //       factory_context.localInfo(), factory_context.dispatcher(),
      // factory_context.clusterManager(), factory_context.random()
      // `factory_context.api(), factory_context.initManager()
            
            return std::make_shared<Router::RouteConfigProviderManagerImpl>(context);
          });

          subscription_ = tap_config_provider_manager->subscribe(config_source, name, *this...)
*/
  }
  default: {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  }
}

ExtensionConfigBase::~ExtensionConfigBase() {
  if (admin_handler_) {
    admin_handler_->unregisterConfig(*this);
  }
}

const absl::string_view ExtensionConfigBase::adminId() {
  // It is only possible to get here if we had an admin config and registered with the admin
  // handler.
  ASSERT(proto_config_.has_admin_config());
  return proto_config_.admin_config().config_id();
}

void ExtensionConfigBase::clearTapConfig() {
  tls_slot_->runOnAllThreads([this] { tls_slot_->getTyped<TlsFilterConfig>().config_ = nullptr; });
}

void ExtensionConfigBase::newTapConfig(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                                       Sink* admin_streamer) {
  tls_slot_->runOnAllThreads(
      [this, proto_config, admin_streamer] {
        // clone the config for this thread, so we can move a copy.
        auto cloned_proto_config = proto_config;
        // Note(yuval-k): Now that createConfigFromProto will initialize a GRPC client, it must be 
        // created and used in the same thread. This implementation moves the initialization to the 
        // worker thread. This will mean that each worker thread will initialize a grpc stream to
        // the tap receiver server.
        // An alternative would be to start it in the main thread (as before), and dispatch the
        // submitTrace events to the main thread - this is a relativly easy change, with the tradoff
        // of risking flooding the main thread.
        TapConfigSharedPtr new_config =
      config_factory_->createConfigFromProto(std::move(cloned_proto_config), admin_streamer);
        tls_slot_->getTyped<TlsFilterConfig>().config_ = new_config; 
      });
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
