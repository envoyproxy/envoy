#include "source/extensions/common/tap/extension_config_base.h"

#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/extensions/common/tap/v3/common.pb.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

ExtensionConfigBase::ExtensionConfigBase(
    const envoy::extensions::common::tap::v3::CommonExtensionConfig proto_config,
    TapConfigFactoryPtr&& config_factory, Server::Admin& admin,
    Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& main_thread_dispatcher)
    : proto_config_(proto_config), config_factory_(std::move(config_factory)), tls_slot_(tls) {
  tls_slot_.set([](Event::Dispatcher&) { return std::make_shared<TlsFilterConfig>(); });

  switch (proto_config_.config_type_case()) {
  case envoy::extensions::common::tap::v3::CommonExtensionConfig::ConfigTypeCase::kAdminConfig: {
    admin_handler_ = AdminHandler::getSingleton(admin, singleton_manager, main_thread_dispatcher);
    admin_handler_->registerConfig(*this, proto_config_.admin_config().config_id());
    ENVOY_LOG(debug, "initializing tap extension with admin endpoint (config_id={})",
              proto_config_.admin_config().config_id());
    break;
  }
  case envoy::extensions::common::tap::v3::CommonExtensionConfig::ConfigTypeCase::kStaticConfig: {
    // Right now only one sink is supported.
    ASSERT(proto_config_.static_config().output_config().sinks().size() == 1);
    if (proto_config_.static_config().output_config().sinks()[0].output_sink_type_case() ==
        envoy::config::tap::v3::OutputSink::OutputSinkTypeCase::kStreamingAdmin) {
      // Require that users do not specify a streaming admin with static configuration.
      throw EnvoyException(
          fmt::format("Error: Specifying admin streaming output without configuring admin."));
    }
    installNewTap(proto_config_.static_config(), nullptr);
    ENVOY_LOG(debug, "initializing tap extension with static config");
    break;
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
  tls_slot_.runOnAllThreads(
      [](OptRef<TlsFilterConfig> tls_filter_config) { tls_filter_config->config_ = nullptr; });
}

void ExtensionConfigBase::installNewTap(const envoy::config::tap::v3::TapConfig& proto_config,
                                        Sink* admin_streamer) {
  TapConfigSharedPtr new_config =
      config_factory_->createConfigFromProto(proto_config, admin_streamer);
  tls_slot_.runOnAllThreads([new_config](OptRef<TlsFilterConfig> tls_filter_config) {
    tls_filter_config->config_ = new_config;
  });
}

void ExtensionConfigBase::newTapConfig(const envoy::config::tap::v3::TapConfig& proto_config,
                                       Sink* admin_streamer) {
  installNewTap(proto_config, admin_streamer);
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
