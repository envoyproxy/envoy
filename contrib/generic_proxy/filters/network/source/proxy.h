#pragma once

#include <memory>
#include <string>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.validate.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/route.pb.h"
#include "contrib/generic_proxy/filters/network/source/interface/codec.h"
#include "contrib/generic_proxy/filters/network/source/interface/filter.h"
#include "contrib/generic_proxy/filters/network/source/interface/route.h"
#include "contrib/generic_proxy/filters/network/source/interface/stream.h"
#include "contrib/generic_proxy/filters/network/source/route.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

using ProxyConfig = envoy::extensions::filters::network::generic_proxy::v3::GenericProxy;
using RouteConfiguration =
    envoy::extensions::filters::network::generic_proxy::v3::RouteConfiguration;

class Filter;
class ActiveStream;

class FilterConfig : public FilterChainFactory {
public:
  FilterConfig(const std::string& stat_prefix, CodecFactoryPtr codec, RouteMatcherPtr route_matcher,
               std::vector<FilterFactoryCb> factories, Server::Configuration::FactoryContext&)
      : stat_prefix_(stat_prefix), codec_factory_(std::move(codec)),
        route_matcher_(std::move(route_matcher)), factories_(std::move(factories)) {}

  FilterConfig(const ProxyConfig& config, Server::Configuration::FactoryContext& context)
      : FilterConfig(config.stat_prefix(), codecFactoryFromProto(config.codec_config(), context),
                     routeMatcherFromProto(config.route_config(), context),
                     filtersFactoryFromProto(config.filters(), config.stat_prefix(), context),
                     context) {}

  RouteEntryConstSharedPtr routeEntry(const Request& request) const {
    return route_matcher_->routeEntry(request);
  }

  // FilterChainFactory
  void createFilterChain(FilterChainFactoryCallbacks& callbacks) override {
    for (const auto& factory : factories_) {
      factory(callbacks);
    }
  }

  const CodecFactory& codecFactory() { return *codec_factory_; }

  static CodecFactoryPtr
  codecFactoryFromProto(const envoy::config::core::v3::TypedExtensionConfig& codec_config,
                        Server::Configuration::FactoryContext& context);

  static RouteMatcherPtr routeMatcherFromProto(const RouteConfiguration& route_config,
                                               Server::Configuration::FactoryContext& context);

  static std::vector<FilterFactoryCb> filtersFactoryFromProto(
      const ProtobufWkt::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>& filters,
      const std::string stats_prefix, Server::Configuration::FactoryContext& context);

private:
  friend class ActiveStream;
  friend class Filter;

  const std::string stat_prefix_;

  CodecFactoryPtr codec_factory_;

  RouteMatcherPtr route_matcher_;

  std::vector<FilterFactoryCb> factories_;
};
using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class ActiveStream : public FilterChainFactoryCallbacks,
                     public DecoderFilterCallback,
                     public EncoderFilterCallback,
                     public LinkedObject<ActiveStream>,
                     public Envoy::Event::DeferredDeletable,
                     public ResponseEncoderCallback,
                     Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  ActiveStream(Filter& parent, RequestPtr request);
  ~ActiveStream() override;

  // FilterChainFactoryCallbacks
  void addDecoderFilter(DecoderFilterSharedPtr filter) override {
    filter->setDecoderFilterCallbacks(*this);
    decoder_filters_.emplace_back(std::move(filter));
  }
  void addEncoderFilter(EncoderFilterSharedPtr filter) override {
    filter->setEncoderFilterCallbacks(*this);
    encoder_filters_.emplace_back(std::move(filter));
  }
  void addFilter(StreamFilterSharedPtr filter) override {
    filter->setDecoderFilterCallbacks(*this);
    filter->setEncoderFilterCallbacks(*this);

    decoder_filters_.push_back(filter);
    encoder_filters_.emplace_back(std::move(filter));
  }

  // StreamFilterCallbacks
  Envoy::Event::Dispatcher& dispatcher() override;
  const CodecFactory& downstreamCodec() override;
  void resetStream() override;
  const RouteEntry* routeEntry() const override { return cached_route_entry_.get(); }

  // DecoderFilterCallback
  void sendLocalReply(Status status, ResponseUpdateFunction&&) override;
  void continueDecoding() override;
  void upstreamResponse(ResponsePtr response) override;
  void completeDirectly() override;

  // EncoderFilterCallback
  void continueEncoding() override;

  // ResponseEncoderCallback
  void onEncodingSuccess(Buffer::Instance& buffer, bool close_connection) override;

  std::vector<DecoderFilterSharedPtr>& decoderFiltersForTest() { return decoder_filters_; }
  std::vector<EncoderFilterSharedPtr>& encoderFiltersForTest() { return encoder_filters_; }
  size_t nextDecoderFilterIndexForTest() { return next_decoder_filter_index_; }
  size_t nextEncoderFilterIndexForTest() { return next_encoder_filter_index_; }

private:
  bool active_stream_reset_{false};

  Filter& parent_;

  RequestPtr downstream_request_stream_;
  ResponsePtr local_or_upstream_response_stream_;

  RouteEntryConstSharedPtr cached_route_entry_;

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
  void onDecodingSuccess(RequestPtr request) override;
  void onDecodingFailure() override;

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

  void sendReplyDownstream(Response& response, ResponseEncoderCallback& callback);

  Network::Connection& connection() {
    ASSERT(callbacks_ != nullptr);
    return callbacks_->connection();
  }

  Server::Configuration::FactoryContext& factoryContext() { return context_; }

  void newDownstreamRequest(RequestPtr request);
  void deferredStream(ActiveStream& stream);

  static const std::string& name() {
    CONSTRUCT_ON_FIRST_USE(std::string, "envoy.filters.network.generic_proxy");
  }

  std::list<ActiveStreamPtr>& activeStreamsForTest() { return active_streams_; }

  void resetStreamsForUnexpectedError();

private:
  friend class ActiveStream;

  bool downstream_connection_closed_{};

  Envoy::Network::ReadFilterCallbacks* callbacks_{nullptr};

  FilterConfigSharedPtr config_{};

  RequestDecoderPtr decoder_;
  ResponseEncoderPtr response_encoder_;
  MessageCreatorPtr creator_;

  Buffer::OwnedImpl response_buffer_;

  Server::Configuration::FactoryContext& context_;

  std::list<ActiveStreamPtr> active_streams_;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
