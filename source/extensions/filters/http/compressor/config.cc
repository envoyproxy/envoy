#include "source/extensions/filters/http/compressor/config.h"

#include "envoy/compression/compressor/config.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/network/address.h"

#include "source/common/config/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/http/compressor/compressor_filter.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {

absl::StatusOr<Http::FilterFactoryCb> CompressorFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::compressor::v3::Compressor& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  const std::string type{TypeUtil::typeUrlToDescriptorFullName(
      proto_config.compressor_library().typed_config().type_url())};
  Compression::Compressor::NamedCompressorLibraryConfigFactory* const config_factory =
      Registry::FactoryRegistry<
          Compression::Compressor::NamedCompressorLibraryConfigFactory>::getFactoryByType(type);
  if (config_factory == nullptr) {
    return absl::InvalidArgumentError(
        fmt::format("Didn't find a registered implementation for type: '{}'", type));
  }
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      proto_config.compressor_library().typed_config(), context.messageValidationVisitor(),
      *config_factory);
  Compression::Compressor::CompressorFactoryPtr compressor_factory =
      config_factory->createCompressorFactoryFromProto(*message, context);
  CompressorFilterConfigSharedPtr config = std::make_shared<CompressorFilterConfig>(
      proto_config, stats_prefix, context.scope(), context.serverFactoryContext().runtime(),
      std::move(compressor_factory));
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<CompressorFilter>(config));
  };
}

namespace {

// Simple null implementations for the route factory context wrapper
struct NullDrainDecision : public Network::DrainDecision {
  bool drainClose(Network::DrainDirection) const override { return false; }
  ::Envoy::Common::CallbackHandlePtr addOnDrainCloseCb(Network::DrainDirection,
                                                       DrainCloseCb) const override {
    return nullptr;
  }
};

struct NullListenerInfo : public Network::ListenerInfo {
  const envoy::config::core::v3::Metadata& metadata() const override {
    static const envoy::config::core::v3::Metadata metadata;
    return metadata;
  }
  const Envoy::Config::TypedMetadata& typedMetadata() const override {
    // Simple empty typed metadata implementation for the null listener info
    class EmptyTypedMetadata : public Envoy::Config::TypedMetadata {
    public:
      const Object* getData(const std::string&) const override { return nullptr; }
    };
    static const EmptyTypedMetadata metadata;
    return metadata;
  }
  envoy::config::core::v3::TrafficDirection direction() const override {
    return envoy::config::core::v3::TrafficDirection::UNSPECIFIED;
  }
  bool isQuic() const override { return false; }
  bool shouldBypassOverloadManager() const override { return false; }
  const Network::Address::Instance& address() const {
    static auto address = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance("0.0.0.0", static_cast<uint32_t>(0))};
    return *address;
  }
  absl::string_view name() const { return "null_listener"; }
  Network::UdpListenerConfigOptRef udpListenerConfig() { return {}; }
};

} // namespace

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
CompressorFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::compressor::v3::CompressorPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validator) {
  // Validate per-route compressor library configuration before creating the config object.
  if (proto_config.has_overrides() && proto_config.overrides().has_compressor_library()) {
    const std::string type{TypeUtil::typeUrlToDescriptorFullName(
        proto_config.overrides().compressor_library().typed_config().type_url())};
    Compression::Compressor::NamedCompressorLibraryConfigFactory* const config_factory =
        Registry::FactoryRegistry<
            Compression::Compressor::NamedCompressorLibraryConfigFactory>::getFactoryByType(type);
    if (config_factory == nullptr) {
      return absl::InvalidArgumentError(fmt::format(
          "Didn't find a registered implementation for per-route compressor type: '{}'", type));
    }
  }

  // Create a temporary factory context that wraps the generic factory context.
  // Other filters commonly use GenericFactoryContextImpl for per-route config construction.
  // Since the compressor library factory requires a FactoryContext, adapt GenericFactoryContextImpl
  // by providing minimal implementations for listener-specific methods.
  Server::GenericFactoryContextImpl generic_context(context, validator);
  struct RouteFactoryContextWrapper : public Server::Configuration::FactoryContext {
    explicit RouteFactoryContextWrapper(Server::GenericFactoryContextImpl& generic)
        : generic_(generic) {}

    // GenericFactoryContext methods
    Server::Configuration::ServerFactoryContext& serverFactoryContext() override {
      return generic_.serverFactoryContext();
    }
    ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
      return generic_.messageValidationVisitor();
    }
    Init::Manager& initManager() override { return generic_.initManager(); }
    Stats::Scope& scope() override { return generic_.scope(); }

    // FactoryContext methods
    Stats::Scope& listenerScope() override { return generic_.scope(); }
    const Network::DrainDecision& drainDecision() override {
      static NullDrainDecision null_drain;
      return null_drain;
    }
    const Network::ListenerInfo& listenerInfo() const override {
      static NullListenerInfo null_listener_info;
      return null_listener_info;
    }

  private:
    Server::GenericFactoryContextImpl& generic_;
  } wrapper(generic_context);

  return std::make_shared<CompressorPerRouteFilterConfig>(proto_config, wrapper);
}

/**
 * Static registration for the compressor filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(CompressorFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
