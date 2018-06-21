#include "extensions/filters/network/thrift_proxy/filter.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"

#include "extensions/filters/network/thrift_proxy/buffer_helper.h"
#include "extensions/filters/network/thrift_proxy/protocol.h"
#include "extensions/filters/network/thrift_proxy/transport.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

Filter::Filter(const std::string& stat_prefix, Stats::Scope& scope)
    : req_callbacks_(*this), resp_callbacks_(*this), stats_(generateStats(stat_prefix, scope)) {}

Filter::~Filter() {}

void Filter::onEvent(Network::ConnectionEvent event) {
  if (active_call_map_.empty() && req_ == nullptr && resp_ == nullptr) {
    return;
  }

  if (event == Network::ConnectionEvent::RemoteClose) {
    stats_.cx_destroy_local_with_active_rq_.inc();
  }

  if (event == Network::ConnectionEvent::LocalClose) {
    stats_.cx_destroy_remote_with_active_rq_.inc();
  }
}

Network::FilterStatus Filter::onData(Buffer::Instance& data, bool) {
  if (!sniffing_) {
    if (req_buffer_.length() > 0) {
      // Stopped sniffing during response (in onWrite). Make sure leftover req_buffer_ contents are
      // at the start of data or the upstream will see a corrupted request.
      req_buffer_.move(data);
      data.move(req_buffer_);
      ASSERT(req_buffer_.length() == 0);
    }

    return Network::FilterStatus::Continue;
  }

  if (req_decoder_ == nullptr) {
    req_decoder_ = std::make_unique<Decoder>(std::make_unique<AutoTransportImpl>(req_callbacks_),
                                             std::make_unique<AutoProtocolImpl>(req_callbacks_));
  }

  ENVOY_LOG(trace, "thrift: read {} bytes", data.length());
  req_buffer_.move(data);

  try {
    BufferWrapper wrapped(req_buffer_);

    req_decoder_->onData(wrapped);

    // Move consumed portion of request back to data for the upstream to consume.
    uint64_t pos = wrapped.position();
    if (pos > 0) {
      data.move(req_buffer_, pos);
    }
  } catch (const EnvoyException& ex) {
    ENVOY_LOG(error, "thrift error: {}", ex.what());
    req_decoder_.reset();
    data.move(req_buffer_);
    stats_.request_decoding_error_.inc();
    sniffing_ = false;
  }

  return Network::FilterStatus::Continue;
}

Network::FilterStatus Filter::onWrite(Buffer::Instance& data, bool) {
  if (!sniffing_) {
    if (resp_buffer_.length() > 0) {
      // Stopped sniffing during request (in onData). Make sure resp_buffer_ contents are at the
      // start of data or the downstream will see a corrupted response.
      resp_buffer_.move(data);
      data.move(resp_buffer_);
      ASSERT(resp_buffer_.length() == 0);
    }

    return Network::FilterStatus::Continue;
  }

  if (resp_decoder_ == nullptr) {
    resp_decoder_ = std::make_unique<Decoder>(std::make_unique<AutoTransportImpl>(resp_callbacks_),
                                              std::make_unique<AutoProtocolImpl>(resp_callbacks_));
  }

  ENVOY_LOG(trace, "thrift wrote {} bytes", data.length());
  resp_buffer_.move(data);

  try {
    BufferWrapper wrapped(resp_buffer_);

    resp_decoder_->onData(wrapped);

    // Move consumed portion of response back to data for the downstream to consume.
    uint64_t pos = wrapped.position();
    if (pos > 0) {
      data.move(resp_buffer_, pos);
    }
  } catch (const EnvoyException& ex) {
    ENVOY_LOG(error, "thrift error: {}", ex.what());
    resp_decoder_.reset();
    data.move(resp_buffer_);

    stats_.response_decoding_error_.inc();
    sniffing_ = false;
  }

  return Network::FilterStatus::Continue;
}

void Filter::chargeDownstreamRequestStart(MessageType msg_type, int32_t seq_id) {
  if (req_ != nullptr) {
    throw EnvoyException("unexpected request messageStart callback");
  }

  if (active_call_map_.size() >= 64) {
    throw EnvoyException("too many pending calls (64), disabling sniffing");
  }

  req_ = std::make_unique<ActiveMessage>(*this, msg_type, seq_id);

  stats_.request_.inc();
  switch (msg_type) {
  case MessageType::Call:
    stats_.request_call_.inc();
    break;
  case MessageType::Oneway:
    stats_.request_oneway_.inc();
    break;
  default:
    stats_.request_invalid_type_.inc();
    break;
  }
}

void Filter::chargeDownstreamRequestComplete() {
  if (req_ == nullptr) {
    throw EnvoyException("unexpected request messageComplete callback");
  }

  // One-way messages do not receive responses.
  if (req_->msg_type_ == MessageType::Oneway) {
    req_.reset();
    return;
  }

  int32_t seq_id = req_->seq_id_;
  active_call_map_.emplace(seq_id, std::move(req_));
}

void Filter::chargeUpstreamResponseStart(MessageType msg_type, int32_t seq_id) {
  if (resp_ != nullptr) {
    throw EnvoyException("unexpected response messageStart callback");
  }

  auto i = active_call_map_.find(seq_id);
  if (i == active_call_map_.end()) {
    throw EnvoyException(fmt::format("unknown reply seq_id {}", seq_id));
  }

  resp_ = std::move(i->second);
  resp_->response_msg_type_ = msg_type;
  active_call_map_.erase(i);
}

void Filter::chargeUpstreamResponseField(FieldType field_type, int16_t field_id) {
  if (resp_ == nullptr) {
    throw EnvoyException("unexpected response messageField callback");
  }

  if (resp_->response_msg_type_ != MessageType::Reply) {
    // If this is not a reply, we'll count an exception instead of an error, so leave
    // resp_->success_ unset.
    return;
  }

  if (resp_->success_.has_value()) {
    // If resp->success_ is already set, leave the existing value.
    return;
  }

  // Successful replies have a single field, with field_id 0 that contains the response value.
  // IDL-level exceptions are encoded as a single field with field_id >= 1.
  resp_->success_ = field_id == 0 && field_type != FieldType::Stop;
}

void Filter::chargeUpstreamResponseComplete() {
  if (resp_ == nullptr) {
    throw EnvoyException("unexpected response messageComplete callback");
  }

  stats_.response_.inc();
  switch (resp_->response_msg_type_) {
  case MessageType::Reply:
    stats_.response_reply_.inc();
    break;
  case MessageType::Exception:
    stats_.response_exception_.inc();
    break;
  default:
    stats_.response_invalid_type_.inc();
    break;
  }

  if (resp_->success_.has_value()) {
    if (resp_->success_.value()) {
      stats_.response_success_.inc();
    } else {
      stats_.response_error_.inc();
    }
  }

  resp_.reset();
}

void Filter::RequestCallbacks::transportFrameStart(absl::optional<uint32_t> size) {
  UNREFERENCED_PARAMETER(size);
  ENVOY_LOG(debug, "thrift request: started {} frame", parent_.req_decoder_->transport().name());
}

void Filter::RequestCallbacks::transportFrameComplete() {
  ENVOY_LOG(debug, "thrift request: ended {} frame", parent_.req_decoder_->transport().name());
}

void Filter::RequestCallbacks::messageStart(const absl::string_view name, MessageType msg_type,
                                            int32_t seq_id) {
  ENVOY_LOG(debug, "thrift request: started {} message {}: {}",
            parent_.req_decoder_->protocol().name(), name, seq_id);
  parent_.chargeDownstreamRequestStart(msg_type, seq_id);
}

void Filter::RequestCallbacks::structBegin(const absl::string_view name) {
  UNREFERENCED_PARAMETER(name);
  ENVOY_LOG(debug, "thrift request: started {} struct", parent_.req_decoder_->protocol().name());
}

void Filter::RequestCallbacks::structField(const absl::string_view name, FieldType field_type,
                                           int16_t field_id) {
  UNREFERENCED_PARAMETER(name);
  ENVOY_LOG(debug, "thrift request: started {} field {}, type {}",
            parent_.req_decoder_->protocol().name(), field_id, static_cast<int8_t>(field_type));
}

void Filter::RequestCallbacks::structEnd() {
  ENVOY_LOG(debug, "thrift request: ended {} struct", parent_.req_decoder_->protocol().name());
}

void Filter::RequestCallbacks::messageComplete() {
  ENVOY_LOG(debug, "thrift request: ended {} message", parent_.req_decoder_->protocol().name());
  parent_.chargeDownstreamRequestComplete();
}

void Filter::ResponseCallbacks::transportFrameStart(absl::optional<uint32_t> size) {
  UNREFERENCED_PARAMETER(size);
  ENVOY_LOG(debug, "thrift response: started {} frame", parent_.resp_decoder_->transport().name());
}

void Filter::ResponseCallbacks::transportFrameComplete() {
  ENVOY_LOG(debug, "thrift response: ended {} frame", parent_.resp_decoder_->transport().name());
}

void Filter::ResponseCallbacks::messageStart(const absl::string_view name, MessageType msg_type,
                                             int32_t seq_id) {
  ENVOY_LOG(debug, "thrift response: started {} message {}: {}",
            parent_.resp_decoder_->protocol().name(), name, seq_id);
  parent_.chargeUpstreamResponseStart(msg_type, seq_id);
}

void Filter::ResponseCallbacks::structBegin(const absl::string_view name) {
  UNREFERENCED_PARAMETER(name);
  ENVOY_LOG(debug, "thrift response: started {} struct", parent_.req_decoder_->protocol().name());
  depth_++;
}

void Filter::ResponseCallbacks::structField(const absl::string_view name, FieldType field_type,
                                            int16_t field_id) {
  UNREFERENCED_PARAMETER(name);
  ENVOY_LOG(debug, "thrift response: started {} field {}, type {}",
            parent_.req_decoder_->protocol().name(), field_id, static_cast<int8_t>(field_type));

  if (depth_ == 1) {
    // Only care about the outermost struct, which corresponds to the success or failure of the
    // request.
    parent_.chargeUpstreamResponseField(field_type, field_id);
  }
}

void Filter::ResponseCallbacks::structEnd() {
  ENVOY_LOG(debug, "thrift request: ended {} struct", parent_.req_decoder_->protocol().name());
  depth_--;
}

void Filter::ResponseCallbacks::messageComplete() {
  ENVOY_LOG(debug, "thrift response: ended {} message", parent_.resp_decoder_->protocol().name());
  parent_.chargeUpstreamResponseComplete();
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
