
#include "contrib/generic_proxy/filters/network/source/generic_proxy.h"

#include "source/common/config/utility.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "envoy/common/exception.h"
#include "envoy/network/connection.h"

#include "contrib/generic_proxy/filters/network/source/interface/generic_config.h"
#include "contrib/generic_proxy/filters/network/source/interface/generic_filter.h"
#include "contrib/generic_proxy/filters/network/source/route_impl.h"

namespace Envoy {
namespace Proxy {
namespace NetworkFilters {
namespace GenericProxy {

CodecFactoryPtr FilterConfig::codecFactoryFromProto(
    const envoy::config::core::v3::TypedExtensionConfig& codec_config,
    Envoy::Server::Configuration::FactoryContext& context) {
  auto& factory =
      Config::Utility::getAndCheckFactoryByName<CodecFactoryConfig>(codec_config.name());

  ProtobufTypes::MessagePtr message = factory.createEmptyConfigProto();
  Envoy::Config::Utility::translateOpaqueConfig(codec_config.typed_config(), ProtobufWkt::Struct(),
                                                context.messageValidationVisitor(), *message);
  return factory.createFactory(*message, context);
}

RouteMatcherPtr FilterConfig::routeMatcherFromProto(
    const envoy::extensions::filters::network::generic_proxy::v3::RouteConfiguration& route_config,
    Envoy::Server::Configuration::FactoryContext& context) {
  return std::make_unique<RouteMatcherImpl>(route_config, context);
}

std::vector<FilterFactoryCb> FilterConfig::filtersFactoryFromProto(
    const google::protobuf::RepeatedPtrField<
        envoy::extensions::filters::network::generic_proxy::v3::GenericFilter>& filters,
    const std::string stats_prefix, Envoy::Server::Configuration::FactoryContext& context) {

  std::vector<FilterFactoryCb> factories;
  bool has_terminal_filter = false;
  std::string terminal_filter_name;
  for (const auto& filter : filters) {
    if (has_terminal_filter) {
      throw EnvoyException(fmt::format("Termial filter: {} must be the last generic L7 filter",
                                       terminal_filter_name));
    }

    auto& factory =
        Config::Utility::getAndCheckFactoryByName<NamedGenericFilterConfigFactory>(filter.name());

    ProtobufTypes::MessagePtr message = factory.createEmptyConfigProto();
    Envoy::Config::Utility::translateOpaqueConfig(filter.config(), ProtobufWkt::Struct(),
                                                  context.messageValidationVisitor(), *message);

    factories.push_back(factory.createFilterFactoryFromProto(*message, stats_prefix, context));

    if (factory.isTerminalFilter()) {
      terminal_filter_name = filter.name();
      has_terminal_filter = true;
    }
  }

  if (!has_terminal_filter) {
    throw EnvoyException("A termial L7 filter is necessary for generic proxy");
  }
  return factories;
}

ActiveStream::ActiveStream(Filter& parent, GenericRequestPtr request)
    : parent_(parent), request_(std::move(request)) {}

ActiveStream::~ActiveStream() {
  for (auto& filter : decoder_filters_) {
    filter->onDestroy();
  }
  for (auto& filter : encoder_filters_) {
    if (filter->isDualFilter()) {
      continue;
    }
    filter->onDestroy();
  }
}

const Network::Connection* ActiveStream::connection() { return &parent_.connection(); }
Event::Dispatcher& ActiveStream::dispatcher() { return parent_.connection().dispatcher(); }
const CodecFactory& ActiveStream::downstreamCodec() { return *parent_.config_->codec_factory_; }
void ActiveStream::resetStream() {
  if (active_stream_reset_) {
    return;
  }
  active_stream_reset_ = true;
  parent_.deferredStream(*this);
}

void ActiveStream::sendLocalReply(GenericState status, absl::string_view status_detail,
                                  MetadataUpdateFunction&& func) {
  parent_.sendLocalReply(status, status_detail, std::move(func));
}

void ActiveStream::continueDecoding() {
  if (active_stream_reset_ || request_ == nullptr) {
    return;
  }

  ASSERT(request_ != nullptr);
  for (; next_decoder_filter_index_ < decoder_filters_.size();) {
    auto status = decoder_filters_[next_decoder_filter_index_]->onStreamDecoded(*request_);
    next_decoder_filter_index_++;
    if (status == GenericFilterStatus::StopIteration) {
      break;
    }
  }
  if (next_decoder_filter_index_ == decoder_filters_.size()) {
    ENVOY_LOG(debug, "Complete decoder filters");
  }
}

void ActiveStream::upstreamResponse(GenericResponsePtr response) {
  response_ = std::move(response);
  continueEncoding();
}

void ActiveStream::continueEncoding() {
  if (active_stream_reset_ || response_ == nullptr) {
    return;
  }

  ASSERT(response_ != nullptr);
  for (; next_encoder_filter_index_ < encoder_filters_.size();) {
    auto status = encoder_filters_[next_encoder_filter_index_]->onStreamEncoded(*response_);
    next_encoder_filter_index_++;
    if (status == GenericFilterStatus::StopIteration) {
      break;
    }
  }

  if (next_encoder_filter_index_ == encoder_filters_.size()) {
    ENVOY_LOG(debug, "Complete decoder filters");
    parent_.sendReplyDownstream(*response_);
  }
}

Envoy::Network::FilterStatus Filter::onData(Envoy::Buffer::Instance& data, bool) {
  if (downstream_connection_closed_) {
    return Envoy::Network::FilterStatus::StopIteration;
  }

  decoder_->decode(data);
  return Envoy::Network::FilterStatus::StopIteration;
}

void Filter::onGenericRequest(GenericRequestPtr request) {
  // New normal request and create active stream for this request.
  newDownstreamRequest(std::move(request));
}

void Filter::onDirectResponse(GenericResponsePtr direct) { sendReplyDownstream(*direct); }

void Filter::onDecodingError() {
  resetStreamsForUnexpectedError();
  connection().close(Network::ConnectionCloseType::FlushWrite);
}

void Filter::sendReplyDownstream(GenericResponse& response) {
  ASSERT(callbacks_->connection().state() == Network::Connection::State::Open);
  response_encoder_->encode(response, response_buffer_);
  callbacks_->connection().write(response_buffer_, false);
}

void Filter::sendLocalReply(GenericState status, absl::string_view status_detail,
                            MetadataUpdateFunction&& func) {
  auto response = creator_->response(status, status_detail);
  func(*response);
  sendReplyDownstream(*response);
}

void Filter::newDownstreamRequest(GenericRequestPtr request) {
  auto stream = std::make_unique<ActiveStream>(*this, std::move(request));
  config_->createFilterChain(*stream);
  LinkedList::moveIntoList(std::move(stream), active_streams_);
  // Start request.
  (*active_streams_.begin())->continueDecoding();
}

void Filter::deferredStream(ActiveStream& stream) {
  if (!stream.inserted()) {
    return;
  }
  callbacks_->connection().dispatcher().deferredDelete(stream.removeFromList(active_streams_));
}

void Filter::resetStreamsForUnexpectedError() {
  while (!active_streams_.empty()) {
    active_streams_.front()->resetStream();
  }
}

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Proxy
} // namespace Envoy
