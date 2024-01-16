#include "library/common/engine_common.h"

#include "source/common/common/random_generator.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/server/null_overload_manager.h"

#if !defined(ENVOY_ENABLE_FULL_PROTOS)

#include "bazel/cc_proto_descriptor_library/file_descriptor_info.h"
#include "source/common/protobuf/protobuf.h"

#include "library/common/extensions/cert_validator/platform_bridge/platform_bridge_descriptor.pb.h"
#include "library/common/extensions/filters/http/local_error/filter_descriptor.pb.h"
#include "library/common/extensions/filters/http/network_configuration/filter_descriptor.pb.h"
#include "library/common/extensions/filters/http/platform_bridge/filter_descriptor.pb.h"
#include "library/common/extensions/filters/http/socket_tag/filter_descriptor.pb.h"
#include "library/common/extensions/key_value/platform/platform_descriptor.pb.h"
#include "library/common/extensions/retry/options/network_configuration/predicate_descriptor.pb.h"

namespace Envoy {

bool initialize() {
  std::vector<FileDescriptorInfo> file_descriptors = {
      protobuf::reflection::
          library_common_extensions_cert_validator_platform_bridge_platform_bridge::
              kFileDescriptorInfo,
      protobuf::reflection::library_common_extensions_filters_http_local_error_filter::
          kFileDescriptorInfo,
      protobuf::reflection::library_common_extensions_filters_http_network_configuration_filter::
          kFileDescriptorInfo,
      protobuf::reflection::library_common_extensions_filters_http_platform_bridge_filter::
          kFileDescriptorInfo,
      protobuf::reflection::library_common_extensions_filters_http_socket_tag_filter::
          kFileDescriptorInfo,
      protobuf::reflection::library_common_extensions_key_value_platform_platform::
          kFileDescriptorInfo,
      protobuf::reflection::
          library_common_extensions_retry_options_network_configuration_predicate::
              kFileDescriptorInfo,
  };
  for (const FileDescriptorInfo& descriptor : file_descriptors) {
    loadFileDescriptors(descriptor);
  }
  return true;
}

void registerMobileProtoDescriptors() {
  static bool initialized = initialize();
  (void)initialized;
}

} // namespace Envoy

#endif

namespace Envoy {

class ServerLite : public Server::InstanceBase {
public:
  using Server::InstanceBase::InstanceBase;
  void maybeCreateHeapShrinker() override {}
  std::unique_ptr<Envoy::Server::OverloadManager> createOverloadManager() override {
    return std::make_unique<Envoy::Server::NullOverloadManager>(threadLocal(), true);
  }
  std::unique_ptr<Server::GuardDog> maybeCreateGuardDog(absl::string_view) override {
    return nullptr;
  }
};

EngineCommon::EngineCommon(std::unique_ptr<Envoy::OptionsImplBase>&& options)
    : options_(std::move(options)) {

#if !defined(ENVOY_ENABLE_FULL_PROTOS)
  registerMobileProtoDescriptors();
#endif

  StrippedMainBase::CreateInstanceFunction create_instance =
      [](Init::Manager& init_manager, const Server::Options& options,
         Event::TimeSystem& time_system, ListenerHooks& hooks, Server::HotRestart& restarter,
         Stats::StoreRoot& store, Thread::BasicLockable& access_log_lock,
         Server::ComponentFactory& component_factory, Random::RandomGeneratorPtr&& random_generator,
         ThreadLocal::Instance& tls, Thread::ThreadFactory& thread_factory,
         Filesystem::Instance& file_system, std::unique_ptr<ProcessContext> process_context,
         Buffer::WatermarkFactorySharedPtr watermark_factory) {
        auto local_address = Network::Utility::getLocalAddress(options.localAddressIpVersion());
        auto server = std::make_unique<ServerLite>(
            init_manager, options, time_system, hooks, restarter, store, access_log_lock,
            std::move(random_generator), tls, thread_factory, file_system,
            std::move(process_context), watermark_factory);
        server->initialize(local_address, component_factory);
        return server;
      };
  base_ = std::make_unique<StrippedMainBase>(
      *options_, real_time_system_, default_listener_hooks_, prod_component_factory_,
      std::make_unique<PlatformImpl>(), std::make_unique<Random::RandomGeneratorImpl>(), nullptr,
      create_instance);
  // Disabling signal handling in the options makes it so that the server's event dispatcher _does
  // not_ listen for termination signals such as SIGTERM, SIGINT, etc
  // (https://github.com/envoyproxy/envoy/blob/048f4231310fbbead0cbe03d43ffb4307fff0517/source/server/server.cc#L519).
  // Previous crashes in iOS were experienced due to early event loop exit as described in
  // https://github.com/envoyproxy/envoy-mobile/issues/831. Ignoring termination signals makes it
  // more likely that the event loop will only exit due to Engine destruction
  // https://github.com/envoyproxy/envoy-mobile/blob/a72a51e64543882ea05fba3c76178b5784d39cdc/library/common/engine.cc#L105.
  options_->setSignalHandling(false);
}

} // namespace Envoy
