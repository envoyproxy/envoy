#pragma once

#include <bits/stdint-uintn.h>
#include <cstddef>
#include <memory>
#include <string>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/buffer/buffer_impl.h"

#include "contrib/generic_proxy/filters/network/source/interface/generic_codec.h"
#include "contrib/generic_proxy/filters/network/source/interface/generic_filter.h"
#include "contrib/generic_proxy/filters/network/source/interface/generic_route.h"
#include "contrib/generic_proxy/filters/network/source/interface/generic_stream.h"
#include "contrib/generic_proxy/filters/network/source/route_impl.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.validate.h"

namespace Envoy {
namespace Proxy {
namespace NetworkFilters {
namespace GenericProxy {

using GenericProxyConfig =
    envoy::extensions::filters::network::generic_proxy::v3::GenericProxyConfig;

class Filter;
class ActiveStream;

class FilterConfig : public FilterChainFactory {
public:
  FilterConfig(const std::string& stat_prefix, CodecFactoryPtr codec, RouteMatcherPtr route_matcher,
               std::vector<FilterFactoryCb> factories,
               Server::Configuration::FactoryContext& context)
      : stat_prefix_(stat_prefix), codec_factory_(std::move(codec)),
        route_matcher_(std::move(route_matcher)), factories_(std::move(factories)),
        context_(context) {}

  FilterConfig(const GenericProxyConfig& config, Server::Configuration::FactoryContext& context)
      : FilterConfig(
            config.stat_prefix(), codecFactoryFromProto(config.codec_specifier(), context),
            routeMatcherFromProto(config.route_config(), context),
            filtersFactoryFromProto(config.generic_filters(), config.stat_prefix(), context),
            context) {}

  RouteEntryConstSharedPtr routeEntry(const GenericRequest& request) const {
    return route_matcher_->routeEntry(request);
  }

  // FilterChainFactory
  void createFilterChain(FilterChainFactoryCallbacks& callbacks) override {
    for (const auto& factory : factories_) {
      factory(callbacks);
    }
  }

  const CodecFactory& codecFactory() { return *codec_factory_; }

private:
  friend class ActiveStream;
  friend class Filter;

  static CodecFactoryPtr
  codecFactoryFromProto(const envoy::config::core::v3::TypedExtensionConfig& codec_config,
                        Server::Configuration::FactoryContext& context);

  static RouteMatcherPtr routeMatcherFromProto(
      const envoy::extensions::filters::network::generic_proxy::v3::RouteConfiguration&
          route_config,
      Server::Configuration::FactoryContext& context);

  static std::vector<FilterFactoryCb> filtersFactoryFromProto(
      const google::protobuf::RepeatedPtrField<
          envoy::extensions::filters::network::generic_proxy::v3::GenericFilter>& filters,
      const std::string stats_prefix, Server::Configuration::FactoryContext& context);

  const std::string stat_prefix_;

  CodecFactoryPtr codec_factory_;

  RouteMatcherPtr route_matcher_;

  std::vector<FilterFactoryCb> factories_;

  Server::Configuration::FactoryContext& context_;
};
using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class ActiveStream : public FilterChainFactoryCallbacks,
                     public DecoderFilterCallback,
                     public EncoderFilterCallback,
                     public LinkedObject<ActiveStream>,
                     public Event::DeferredDeletable,
                     Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  ActiveStream(Filter& parent, GenericRequestPtr request);
  ~ActiveStream();

  // FilterChainFactoryCallbacks
  void addDecoderFilter(DecoderFilterSharedPtr filter) override {
    decoder_filters_.emplace_back(std::move(filter));
  }
  void addEncoderFilter(EncoderFilterSharedPtr filter) override {
    encoder_filters_.emplace_back(std::move(filter));
  }
  void addFilter(StreamFilterSharedPtr filter) override {
    decoder_filters_.push_back(filter);
    encoder_filters_.emplace_back(std::move(filter));
  }

  // StreamFilterCallbacks
  const Network::Connection* connection() override;
  Event::Dispatcher& dispatcher() override;
  const CodecFactory& downstreamCodec() override;
  void resetStream() override;
  const RouteEntry* routeEntry() const override { return cached_route_entry_.get(); }
  Upstream::ClusterInfoConstSharedPtr clusterInfo() override { return cached_cluster_info_; }

  // DecoderFilterCallback
  void sendLocalReply(GenericState status, absl::string_view status_detail,
                      MetadataUpdateFunction&&) override;
  void continueDecoding() override;
  void upstreamResponse(GenericResponsePtr response) override;

  // EncoderFilterCallback
  void continueEncoding() override;

private:
  Filter& parent_;

  bool active_stream_reset_{false};

  GenericRequestPtr request_;
  GenericResponsePtr response_;

  RouteEntryConstSharedPtr cached_route_entry_;
  Upstream::ClusterInfoConstSharedPtr cached_cluster_info_;

  std::vector<DecoderFilterSharedPtr> decoder_filters_;
  size_t next_decoder_filter_index_{0};

  std::vector<EncoderFilterSharedPtr> encoder_filters_;
  size_t next_encoder_filter_index_{0};
};
using ActiveStreamPtr = std::unique_ptr<ActiveStream>;

class Filter : public Envoy::Network::ReadFilter,
               public Network::ConnectionCallbacks,
               public Envoy::Logger::Loggable<Envoy::Logger::Id::filter>,
               public RequestDecoderCallback {
public:
  Filter(FilterConfigSharedPtr config, Server::Configuration::FactoryContext& context)
      : config_(std::move(config)), context_(context) {
    decoder_ = config_->codecFactory().requestDecoder();
    decoder_->setDecoderCallback(*this);
    response_encoder_ = config_->codecFactory().responseEncoder();
    creator_ = config_->codecFactory().messageCreator();
  }

  // Envoy::Network::ReadFilter
  Envoy::Network::FilterStatus onData(Envoy::Buffer::Instance& data, bool end_stream) override;
  Envoy::Network::FilterStatus onNewConnection() override {
    return Envoy::Network::FilterStatus::Continue;
  }
  void initializeReadFilterCallbacks(Envoy::Network::ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    callbacks_->connection().addConnectionCallbacks(*this);
  }

  // RequestDecoderCallback
  void onGenericRequest(GenericRequestPtr request) override;
  void onDirectResponse(GenericResponsePtr direct) override;
  void onDecodingError() override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override {
    if (event == Network::ConnectionEvent::Connected) {
      return;
    }
    downstream_connection_closed_ = true;
    resetStreamsForUnexpectedError();
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  void sendReplyDownstream(GenericResponse& response);
  void sendLocalReply(GenericState status, absl::string_view status_detail,
                      MetadataUpdateFunction&&);

  Network::Connection& connection() {
    ASSERT(callbacks_ != nullptr);
    return callbacks_->connection();
  }

  Server::Configuration::FactoryContext& factoryContext() { return config_->context_; }

  void newDownstreamRequest(GenericRequestPtr request);
  void deferredStream(ActiveStream& stream);

  static const std::string& name() {
    CONSTRUCT_ON_FIRST_USE(std::string, "envoy.filters.network.generic_proxy");
  }

private:
  friend class ActiveStream;

  void resetStreamsForUnexpectedError();

  bool downstream_connection_closed_{};

  Envoy::Network::ReadFilterCallbacks* callbacks_{nullptr};

  FilterConfigSharedPtr config_{};

  GenericRequestDecoderPtr decoder_;
  GenericResponseEncoderPtr response_encoder_;
  GenericMessageCreatorPtr creator_;

  Buffer::OwnedImpl response_buffer_;

  Server::Configuration::FactoryContext& context_;

  std::list<ActiveStreamPtr> active_streams_;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Proxy
} // namespace Envoy
