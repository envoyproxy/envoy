#include "extensions/filters/network/dubbo_proxy/filter.h"

#include "envoy/common/exception.h"

#include "common/common/fmt.h"

#include "extensions/filters/network/dubbo_proxy/buffer_helper.h"
#include "extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"
#include "extensions/filters/network/dubbo_proxy/hessian_deserializer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

Filter::Filter(const std::string& stat_prefix, ConfigProtocolType protocol_type,
               ConfigSerializationType serialization_type, Stats::Scope& scope,
               TimeSource& time_source)
    : stats_(generateStats(stat_prefix, scope)), protocol_type_(protocol_type),
      serialization_type_(serialization_type), time_source_(time_source) {}

Filter::~Filter() = default;

Network::FilterStatus Filter::onData(Buffer::Instance& data, bool) {
  if (!sniffing_) {
    if (request_buffer_.length() > 0) {
      // Stopped sniffing during response (in onWrite). Make sure leftover request_buffer_ contents
      // are at the start of data or the upstream will see a corrupted request.
      request_buffer_.move(data);
      data.move(request_buffer_);
      ASSERT(request_buffer_.length() == 0);
    }

    return Network::FilterStatus::Continue;
  }

  ENVOY_LOG(trace, "dubbo: read {} bytes", data.length());
  request_buffer_.move(data);

  try {
    if (!request_decoder_) {
      request_decoder_ = createDecoder(*this);
    }

    BufferWrapper wrapped(request_buffer_);
    request_decoder_->onData(wrapped);

    // Move consumed portion of request back to data for the upstream to consume.
    uint64_t pos = wrapped.position();
    if (pos > 0) {
      data.move(request_buffer_, pos);
    }
  } catch (const EnvoyException& ex) {
    ENVOY_LOG(error, "dubbo: error {}", ex.what());
    data.move(request_buffer_);
    stats_.request_decoding_error_.inc();
    sniffing_ = false;
  }

  return Network::FilterStatus::Continue;
}

Network::FilterStatus Filter::onWrite(Buffer::Instance& data, bool) {
  if (!sniffing_) {
    if (response_buffer_.length() > 0) {
      // Stopped sniffing during request (in onData). Make sure response_buffer_ contents are at the
      // start of data or the downstream will see a corrupted response.
      response_buffer_.move(data);
      data.move(response_buffer_);
      ASSERT(response_buffer_.length() == 0);
    }

    return Network::FilterStatus::Continue;
  }

  ENVOY_LOG(trace, "dubbo: wrote {} bytes", data.length());
  response_buffer_.move(data);

  try {
    if (!response_decoder_) {
      response_decoder_ = createDecoder(*this);
    }

    BufferWrapper wrapped(response_buffer_);
    response_decoder_->onData(wrapped);

    // Move consumed portion of response back to data for the downstream to consume.
    uint64_t pos = wrapped.position();
    if (pos > 0) {
      data.move(response_buffer_, pos);
    }
  } catch (const EnvoyException& ex) {
    ENVOY_LOG(error, "dubbo: error {}", ex.what());
    data.move(response_buffer_);
    stats_.response_decoding_error_.inc();
    sniffing_ = false;
  }

  return Network::FilterStatus::Continue;
}

void Filter::onEvent(Network::ConnectionEvent event) {
  if (active_call_map_.empty()) {
    return;
  }

  if (event == Network::ConnectionEvent::RemoteClose) {
    stats_.cx_destroy_local_with_active_rq_.inc();
  }

  if (event == Network::ConnectionEvent::LocalClose) {
    stats_.cx_destroy_remote_with_active_rq_.inc();
  }
}

void Filter::onRequestMessage(RequestMessagePtr&& message) {
  ASSERT(message);
  ASSERT(message->messageType() == MessageType::Request);

  stats_.request_.inc();
  message->isTwoWay() ? stats_.request_twoway_.inc() : stats_.request_oneway_.inc();

  if (message->isEvent()) {
    stats_.request_event_.inc();
  }

  ENVOY_LOG(debug, "dubbo request: started {} message", message->requestId());

  // One-way messages do not receive responses.
  if (!message->isTwoWay()) {
    return;
  }

  auto request = std::make_unique<ActiveMessage>(*this, message->requestId());
  active_call_map_.emplace(message->requestId(), std::move(request));
}

void Filter::onResponseMessage(ResponseMessagePtr&& message) {
  ASSERT(message);
  ASSERT(message->messageType() == MessageType::Response);

  auto itor = active_call_map_.find(message->requestId());
  if (itor == active_call_map_.end()) {
    throw EnvoyException(fmt::format("unknown request id {}", message->requestId()));
  }
  active_call_map_.erase(itor);

  ENVOY_LOG(debug, "dubbo response: ended {} message", message->requestId());

  stats_.response_.inc();
  switch (message->responseStatus()) {
  case ResponseStatus::Ok:
    stats_.response_success_.inc();
    break;
  default:
    stats_.response_error_.inc();
    ENVOY_LOG(error, "dubbo response status: {}", static_cast<uint8_t>(message->responseStatus()));
    break;
  }
}

void Filter::onRpcInvocation(RpcInvocationPtr&& invo) {
  ENVOY_LOG(debug, "dubbo request: method name is {}, service name is {}, service version {}",
            invo->getMethodName(), invo->getServiceName(), invo->getServiceVersion());
}

void Filter::onRpcResult(RpcResultPtr&& res) {
  if (res->hasException()) {
    stats_.response_exception_.inc();
  }
}

DubboFilterStats Filter::generateStats(const std::string& prefix, Stats::Scope& scope) {
  return DubboFilterStats{ALL_DUBBO_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                                 POOL_GAUGE_PREFIX(scope, prefix),
                                                 POOL_HISTOGRAM_PREFIX(scope, prefix))};
}

DecoderPtr Filter::createDecoder(ProtocolCallbacks& prot_callback) {
  auto parser = createProtocol(prot_callback);
  auto serializer = createDeserializer();
  return std::make_unique<Decoder>(std::move(parser), std::move(serializer), *this);
}

ProtocolPtr Filter::createProtocol(ProtocolCallbacks& callback) {
  using Type = envoy::extensions::filters::network::dubbo_proxy::v2alpha1::DubboProxy;
  switch (protocol_type_) {
  case Type::Dubbo:
    return std::make_unique<DubboProtocolImpl>(callback);
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

DeserializerPtr Filter::createDeserializer() {
  using Type = envoy::extensions::filters::network::dubbo_proxy::v2alpha1::DubboProxy;
  switch (serialization_type_) {
  case Type::Hessian2:
    return std::make_unique<HessianDeserializerImpl>();
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy