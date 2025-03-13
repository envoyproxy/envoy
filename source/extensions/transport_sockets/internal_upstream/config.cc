#include "source/extensions/transport_sockets/internal_upstream/config.h"

#include "envoy/common/hashable.h"
#include "envoy/common/optref.h"
#include "envoy/extensions/transport_sockets/internal_upstream/v3/internal_upstream.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/config/utility.h"
#include "source/extensions/transport_sockets/internal_upstream/internal_upstream.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace InternalUpstream {

namespace {

class InternalUpstreamConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.internal_upstream"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::transport_sockets::internal_upstream::v3::InternalUpstreamTransport>();
  }
  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr> createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override {
    const auto& outer_config =
        MessageUtil::downcastAndValidate<const envoy::extensions::transport_sockets::
                                             internal_upstream::v3::InternalUpstreamTransport&>(
            config, context.messageValidationVisitor());
    auto& inner_config_factory = Envoy::Config::Utility::getAndCheckFactory<
        Server::Configuration::UpstreamTransportSocketConfigFactory>(
        outer_config.transport_socket());
    ProtobufTypes::MessagePtr inner_factory_config =
        Envoy::Config::Utility::translateToFactoryConfig(outer_config.transport_socket(),
                                                         context.messageValidationVisitor(),
                                                         inner_config_factory);
    auto factory_or_error =
        inner_config_factory.createTransportSocketFactory(*inner_factory_config, context);
    RETURN_IF_NOT_OK_REF(factory_or_error.status());
    return std::make_unique<InternalSocketFactory>(context, outer_config,
                                                   std::move(factory_or_error.value()));
  }
};

} // namespace

Config::Config(
    const envoy::extensions::transport_sockets::internal_upstream::v3::InternalUpstreamTransport&
        config_proto,
    Stats::Scope& scope)
    : stats_{ALL_INTERNAL_UPSTREAM_STATS(POOL_COUNTER_PREFIX(scope, "internal_upstream."))} {
  for (const auto& metadata : config_proto.passthrough_metadata()) {
    MetadataKind kind;
    switch (metadata.kind().kind_case()) {
    case envoy::type::metadata::v3::MetadataKind::KindCase::kHost:
      kind = MetadataKind::Host;
      break;
    case envoy::type::metadata::v3::MetadataKind::KindCase::kCluster:
      kind = MetadataKind::Cluster;
      break;
    default:
      throw EnvoyException(
          absl::StrCat("metadata type is not supported: ", metadata.kind().DebugString()));
    }
    metadata_sources_.push_back(MetadataSource(kind, metadata.name()));
  }
}

std::unique_ptr<envoy::config::core::v3::Metadata>
Config::extractMetadata(const Upstream::HostDescriptionConstSharedPtr& host) const {
  if (metadata_sources_.empty()) {
    return nullptr;
  }
  auto metadata = std::make_unique<envoy::config::core::v3::Metadata>();
  for (const auto& source : metadata_sources_) {
    OptRef<const envoy::config::core::v3::Metadata> source_metadata;
    switch (source.kind_) {
    case MetadataKind::Host: {
      if (host->metadata()) {
        source_metadata = makeOptRef(*host->metadata());
      }
      break;
    }
    case MetadataKind::Cluster:
      source_metadata = makeOptRef(host->cluster().metadata());
      break;
    }
    if (source_metadata && source_metadata->filter_metadata().contains(source.name_)) {
      (*metadata->mutable_filter_metadata())[source.name_] =
          source_metadata->filter_metadata().at(source.name_);
    } else {
      ENVOY_LOG(trace, "Internal upstream missing metadata: {}", source.name_);
      stats_.no_metadata_.inc();
    }
  }
  return metadata;
}

InternalSocketFactory::InternalSocketFactory(
    Server::Configuration::TransportSocketFactoryContext& context,
    const envoy::extensions::transport_sockets::internal_upstream::v3::InternalUpstreamTransport&
        config_proto,
    Network::UpstreamTransportSocketFactoryPtr&& inner_factory)
    : PassthroughFactory(std::move(inner_factory)), config_(config_proto, context.statsScope()) {}

Network::TransportSocketPtr
InternalSocketFactory::createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                                             Upstream::HostDescriptionConstSharedPtr host) const {
  auto inner_socket = transport_socket_factory_->createTransportSocket(options, host);
  if (inner_socket == nullptr) {
    return nullptr;
  }
  std::unique_ptr<envoy::config::core::v3::Metadata> extracted_metadata;
  if (host) {
    extracted_metadata = config_.extractMetadata(host);
  }
  return std::make_unique<InternalSocket>(std::move(inner_socket), std::move(extracted_metadata),
                                          options ? options->downstreamSharedFilterStateObjects()
                                                  : StreamInfo::FilterState::Objects());
}

REGISTER_FACTORY(InternalUpstreamConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace InternalUpstream
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
