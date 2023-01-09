#include "contrib/generic_proxy/filters/network/source/proxy.h"

#include "envoy/common/exception.h"
#include "envoy/network/connection.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "contrib/generic_proxy/filters/network/source/interface/config.h"
#include "contrib/generic_proxy/filters/network/source/interface/filter.h"
#include "contrib/generic_proxy/filters/network/source/route.h"

#include "source/common/tracing/custom_tag_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

namespace {
using ProtoTracingConfig = envoy::extensions::filters::network::http_connection_manager::v3::
    HttpConnectionManager::Tracing;

// TODO(wbpcode): Move
TracingConnectionManagerConfigPtr
getTracingConfig(const ProtoTracingConfig& tracing_config,
                 envoy::config::core::v3::TrafficDirection direction) {

  Tracing::OperationName tracing_operation_name;

  // Listener level traffic direction overrides the operation name
  switch (direction) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::core::v3::UNSPECIFIED:
    // Continuing legacy behavior; if unspecified, we treat this as ingress.
    tracing_operation_name = Tracing::OperationName::Ingress;
    break;
  case envoy::config::core::v3::INBOUND:
    tracing_operation_name = Tracing::OperationName::Ingress;
    break;
  case envoy::config::core::v3::OUTBOUND:
    tracing_operation_name = Tracing::OperationName::Egress;
    break;
  }

  Tracing::CustomTagMap custom_tags;
  for (const auto& tag : tracing_config.custom_tags()) {
    custom_tags.emplace(tag.tag(), Tracing::CustomTagUtility::createCustomTag(tag));
  }

  envoy::type::v3::FractionalPercent client_sampling;
  client_sampling.set_numerator(
      tracing_config.has_client_sampling() ? tracing_config.client_sampling().value() : 100);
  envoy::type::v3::FractionalPercent random_sampling;
  // TODO: Random sampling historically was an integer and default to out of 10,000. We should
  // deprecate that and move to a straight fractional percent config.
  uint64_t random_sampling_numerator{PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
      tracing_config, random_sampling, 10000, 10000)};
  random_sampling.set_numerator(random_sampling_numerator);
  random_sampling.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
  envoy::type::v3::FractionalPercent overall_sampling;
  uint64_t overall_sampling_numerator{PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
      tracing_config, overall_sampling, 10000, 10000)};
  overall_sampling.set_numerator(overall_sampling_numerator);
  overall_sampling.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);

  const uint32_t max_path_tag_length = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      tracing_config, max_path_tag_length, Tracing::DefaultMaxPathTagLength);

  return std::make_unique<TracingConnectionManagerConfig>(TracingConnectionManagerConfig{
      tracing_operation_name, custom_tags, client_sampling, random_sampling, overall_sampling,
      tracing_config.verbose(), max_path_tag_length});
}
} // namespace

FilterConfigImpl::FilterConfigImpl(const ProxyConfig& proto_config, const std::string& stat_prefix,
                                   CodecFactoryPtr codec,
                                   Rds::RouteConfigProviderSharedPtr route_config_provider,
                                   std::vector<NamedFilterFactoryCb> factories,
                                   Envoy::Server::Configuration::FactoryContext& context)
    : stat_prefix_(stat_prefix), codec_factory_(std::move(codec)),
      route_config_provider_(std::move(route_config_provider)), factories_(std::move(factories)),
      drain_decision_(context.drainDecision()) {

  if (proto_config.has_tracing()) {
    if (!proto_config.tracing().has_provider()) {
      throw EnvoyException("generic proxy: tracing configuration must have provider");
    }

    tracing_config_ = getTracingConfig(proto_config.tracing(), context.direction());
  }
}

ActiveStream::ActiveStream(Filter& parent, RequestPtr request)
    : parent_(parent), downstream_request_stream_(std::move(request)) {}

ActiveStream::~ActiveStream() {
  for (auto& filter : decoder_filters_) {
    filter->filter_->onDestroy();
  }
  for (auto& filter : encoder_filters_) {
    if (filter->isDualFilter()) {
      continue;
    }
    filter->filter_->onDestroy();
  }
}

Envoy::Event::Dispatcher& ActiveStream::dispatcher() { return parent_.connection().dispatcher(); }
const CodecFactory& ActiveStream::downstreamCodec() { return parent_.config_->codecFactory(); }
void ActiveStream::resetStream() {
  if (active_stream_reset_) {
    return;
  }
  active_stream_reset_ = true;
  parent_.deferredStream(*this);
}

void ActiveStream::sendLocalReply(Status status, ResponseUpdateFunction&& func) {
  ASSERT(parent_.creator_ != nullptr);
  local_or_upstream_response_stream_ =
      parent_.creator_->response(status, *downstream_request_stream_);

  ASSERT(local_or_upstream_response_stream_ != nullptr);

  if (func != nullptr) {
    func(*local_or_upstream_response_stream_);
  }

  parent_.sendReplyDownstream(*local_or_upstream_response_stream_, *this);
}

void ActiveStream::continueDecoding() {
  if (active_stream_reset_ || downstream_request_stream_ == nullptr) {
    return;
  }

  if (cached_route_entry_ == nullptr) {
    cached_route_entry_ = parent_.config_->routeEntry(*downstream_request_stream_);
  }

  ASSERT(downstream_request_stream_ != nullptr);
  for (; next_decoder_filter_index_ < decoder_filters_.size();) {
    auto status = decoder_filters_[next_decoder_filter_index_]->filter_->onStreamDecoded(
        *downstream_request_stream_);
    next_decoder_filter_index_++;
    if (status == FilterStatus::StopIteration) {
      break;
    }
  }
  if (next_decoder_filter_index_ == decoder_filters_.size()) {
    ENVOY_LOG(debug, "Complete decoder filters");
  }
}

void ActiveStream::upstreamResponse(ResponsePtr response) {
  local_or_upstream_response_stream_ = std::move(response);
  continueEncoding();
}

void ActiveStream::completeDirectly() { parent_.deferredStream(*this); };

void ActiveStream::continueEncoding() {
  if (active_stream_reset_ || local_or_upstream_response_stream_ == nullptr) {
    return;
  }

  ASSERT(local_or_upstream_response_stream_ != nullptr);
  for (; next_encoder_filter_index_ < encoder_filters_.size();) {
    auto status = encoder_filters_[next_encoder_filter_index_]->filter_->onStreamEncoded(
        *local_or_upstream_response_stream_);
    next_encoder_filter_index_++;
    if (status == FilterStatus::StopIteration) {
      break;
    }
  }

  if (next_encoder_filter_index_ == encoder_filters_.size()) {
    ENVOY_LOG(debug, "Complete decoder filters");
    parent_.sendReplyDownstream(*local_or_upstream_response_stream_, *this);
  }
}

void ActiveStream::onEncodingSuccess(Buffer::Instance& buffer, bool close_connection) {
  ASSERT(parent_.connection().state() == Network::Connection::State::Open);
  parent_.connection().write(buffer, close_connection);
  parent_.deferredStream(*this);
}

void ActiveStream::initializeFilterChain(FilterChainFactory& factory) {
  factory.createFilterChain(*this);
  // Reverse the encoder filter chain so that the first encoder filter is the last filter in the
  // chain.
  std::reverse(encoder_filters_.begin(), encoder_filters_.end());
}

Envoy::Network::FilterStatus Filter::onData(Envoy::Buffer::Instance& data, bool) {
  if (downstream_connection_closed_) {
    return Envoy::Network::FilterStatus::StopIteration;
  }

  decoder_->decode(data);
  return Envoy::Network::FilterStatus::StopIteration;
}

void Filter::onDecodingSuccess(RequestPtr request) { newDownstreamRequest(std::move(request)); }

void Filter::onDecodingFailure() {
  resetStreamsForUnexpectedError();
  connection().close(Network::ConnectionCloseType::FlushWrite);
}

void Filter::sendReplyDownstream(Response& response, ResponseEncoderCallback& callback) {
  response_encoder_->encode(response, callback);
}

void Filter::newDownstreamRequest(RequestPtr request) {
  auto stream = std::make_unique<ActiveStream>(*this, std::move(request));
  auto raw_stream = stream.get();
  LinkedList::moveIntoList(std::move(stream), active_streams_);

  // Initialize filter chian.
  raw_stream->initializeFilterChain(*config_);
  // Start request.
  raw_stream->continueDecoding();
}

void Filter::deferredStream(ActiveStream& stream) {
  if (!stream.inserted()) {
    return;
  }
  callbacks_->connection().dispatcher().deferredDelete(stream.removeFromList(active_streams_));
  mayBeDrainClose();
}

void Filter::resetStreamsForUnexpectedError() {
  while (!active_streams_.empty()) {
    active_streams_.front()->resetStream();
  }
}

void Filter::mayBeDrainClose() {
  if (drain_decision_.drainClose() && active_streams_.empty()) {
    onDrainCloseAndNoActiveStreams();
  }
}

// Default implementation for connection draining.
void Filter::onDrainCloseAndNoActiveStreams() {
  connection().close(Network::ConnectionCloseType::FlushWrite);
}

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
