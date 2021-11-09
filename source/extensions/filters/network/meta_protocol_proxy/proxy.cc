#include "source/extensions/filters/network/meta_protocol_proxy/proxy.h"

#include "envoy/common/exception.h"
#include "envoy/network/connection.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/network/meta_protocol_proxy/interface/config.h"
#include "source/extensions/filters/network/meta_protocol_proxy/interface/filter.h"
#include "source/extensions/filters/network/meta_protocol_proxy/route.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

CodecFactoryPtr FilterConfig::codecFactoryFromProto(
    const envoy::config::core::v3::TypedExtensionConfig& codec_config,
    Envoy::Server::Configuration::FactoryContext& context) {
  auto& factory =
      Config::Utility::getAndCheckFactoryByName<CodecFactoryConfig>(codec_config.name());

  ProtobufTypes::MessagePtr message = factory.createEmptyConfigProto();
  Envoy::Config::Utility::translateOpaqueConfig(codec_config.typed_config(),
                                                context.messageValidationVisitor(), *message);
  return factory.createFactory(*message, context);
}

RouteMatcherPtr
FilterConfig::routeMatcherFromProto(const RouteConfiguration& route_config,
                                    Envoy::Server::Configuration::FactoryContext& context) {
  return std::make_unique<RouteMatcherImpl>(route_config, context);
}

std::vector<FilterFactoryCb> FilterConfig::filtersFactoryFromProto(
    const ProtobufWkt::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>& filters,
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
        Config::Utility::getAndCheckFactoryByName<NamedFilterConfigFactory>(filter.name());

    ProtobufTypes::MessagePtr message = factory.createEmptyConfigProto();
    Envoy::Config::Utility::translateOpaqueConfig(filter.typed_config(),
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

ActiveStream::ActiveStream(Filter& parent, RequestPtr request)
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
Envoy::Event::Dispatcher& ActiveStream::dispatcher() { return parent_.connection().dispatcher(); }
const CodecFactory& ActiveStream::downstreamCodec() { return *parent_.config_->codec_factory_; }
void ActiveStream::resetStream() {
  if (active_stream_reset_) {
    return;
  }
  active_stream_reset_ = true;
  parent_.deferredStream(*this);
}

void ActiveStream::sendLocalReply(Status status, absl::string_view status_detail,
                                  ResponseUpdateFunction&& func) {
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
    if (status == FilterStatus::StopIteration) {
      break;
    }
  }
  if (next_decoder_filter_index_ == decoder_filters_.size()) {
    ENVOY_LOG(debug, "Complete decoder filters");
  }
}

void ActiveStream::upstreamResponse(ResponsePtr response) {
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
    if (status == FilterStatus::StopIteration) {
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

void Filter::onRequest(RequestPtr request) {
  // New normal request and create active stream for this request.
  newDownstreamRequest(std::move(request));
}

void Filter::onDirectResponse(ResponsePtr direct) { sendReplyDownstream(*direct); }

void Filter::onDecodingError() {
  resetStreamsForUnexpectedError();
  connection().close(Network::ConnectionCloseType::FlushWrite);
}

void Filter::sendReplyDownstream(Response& response) {
  ASSERT(callbacks_->connection().state() == Network::Connection::State::Open);
  response_encoder_->encode(response, response_buffer_);
  callbacks_->connection().write(response_buffer_, false);
}

void Filter::sendLocalReply(Status status, absl::string_view status_detail,
                            ResponseUpdateFunction&& func) {
  auto response = creator_->response(status, status_detail);
  func(*response);
  sendReplyDownstream(*response);
}

void Filter::newDownstreamRequest(RequestPtr request) {
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

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
