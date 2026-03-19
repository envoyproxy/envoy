#include "contrib/ldap_proxy/filters/network/source/ldap_filter.h"

#include <chrono>
#include <vector>

#include "absl/strings/string_view.h"

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LdapProxy {

constexpr uint32_t kUpstreamStartTlsMsgId = 1;
constexpr uint64_t kMaxPendingDownstreamDataSize = 64 * 1024;
constexpr uint64_t kMaxUpstreamBufferSize = 16 * 1024;
constexpr std::chrono::seconds kUpstreamStartTlsTimeout{30};

LdapFilter::LdapFilter(Stats::Scope& scope, bool force_upstream_starttls)
    : stats_(generateStats(scope)),
      decoder_(std::make_unique<DecoderImpl>(this)),
      upstream_starttls_(force_upstream_starttls),
      upstream_starttls_msg_id_(kUpstreamStartTlsMsgId) {}

Network::FilterStatus LdapFilter::onNewConnection() {
  ENVOY_CONN_LOG(trace, "ldap_proxy: new connection, upstream_starttls={}",
                 read_callbacks_->connection(), upstream_starttls_);

  if (isDownstreamTls()) {
    ENVOY_CONN_LOG(debug, "ldap_proxy: downstream is TLS, bypassing inspection",
                   read_callbacks_->connection());
    state_ = FilterState::Passthrough;
    decoder_->setEncrypted();
  }

  return Network::FilterStatus::Continue;
}

bool LdapFilter::isDownstreamTls() const {
  return read_callbacks_->connection().ssl().get() != nullptr;
}

void LdapFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  read_callbacks_->connection().addConnectionCallbacks(*this);
}

void LdapFilter::initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) {
  write_callbacks_ = &callbacks;
}

Network::FilterStatus LdapFilter::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(trace, "ldap_proxy: received {} bytes from downstream, end_stream={}", 
                 read_callbacks_->connection(), data.length(), end_stream);

  switch (state_) {
  case FilterState::Inspecting: {
    if (upstream_starttls_) {
      ENVOY_CONN_LOG(debug, "ldap_proxy: upstream_starttls mode, initiating upstream TLS",
                     read_callbacks_->connection());
      pending_downstream_data_.move(data);
      initiateUpstreamStartTls();
      return Network::FilterStatus::StopIteration;
    }

    inspect_buffer_.move(data);

    if (inspect_buffer_.length() > kMaxInspectBufferSize) {
      ENVOY_CONN_LOG(debug, "ldap_proxy: inspect buffer exceeded max size",
                     read_callbacks_->connection());
      onDecoderError();
      return Network::FilterStatus::StopIteration;
    }

    DecoderSignal signal = decoder_->inspect(inspect_buffer_);

    switch (signal) {
    case DecoderSignal::NeedMoreData:
      return Network::FilterStatus::StopIteration;

    case DecoderSignal::StartTlsDetected:
      return Network::FilterStatus::StopIteration;

    case DecoderSignal::PlaintextOperation:
      ENVOY_CONN_LOG(debug, "ldap_proxy: plaintext operation detected, switching to passthrough",
                     read_callbacks_->connection());
      state_ = FilterState::Passthrough;
      decoder_->setEncrypted();
      read_callbacks_->injectReadDataToFilterChain(inspect_buffer_, false);
      inspect_buffer_.drain(inspect_buffer_.length());
      return Network::FilterStatus::Continue;

    case DecoderSignal::DecoderError:
    case DecoderSignal::PipelineViolation:
      return Network::FilterStatus::StopIteration;
    }
    break;
  }

  case FilterState::NegotiatingUpstream:
    if (upstream_starttls_) {
      if (end_stream) {
        ENVOY_CONN_LOG(debug, "ldap_proxy: downstream closed during upstream TLS upgrade",
                       read_callbacks_->connection());
        closeConnection();
        return Network::FilterStatus::StopIteration;
      }
      
      ENVOY_CONN_LOG(trace, "ldap_proxy: buffering {} bytes during upstream TLS upgrade",
                     read_callbacks_->connection(), data.length());
      
      if (pending_downstream_data_.length() + data.length() > kMaxPendingDownstreamDataSize) {
        ENVOY_CONN_LOG(error, "ldap_proxy: pending downstream data exceeded max size ({})",
                       read_callbacks_->connection(), kMaxPendingDownstreamDataSize);
        stats_.decoder_error_.inc();
        closeConnection();
        return Network::FilterStatus::StopIteration;
      }
      
      pending_downstream_data_.move(data);
      return Network::FilterStatus::StopIteration;
    }
    [[fallthrough]];

  case FilterState::NegotiatingDownstream:
    ENVOY_CONN_LOG(debug, "ldap_proxy: data received during StartTLS negotiation - violation",
                   read_callbacks_->connection());
    onPipelineViolation();
    return Network::FilterStatus::StopIteration;

  case FilterState::Passthrough:
    return Network::FilterStatus::Continue;

  case FilterState::Closed:
    data.drain(data.length());
    return Network::FilterStatus::StopIteration;
  }

  return Network::FilterStatus::Continue;
}

Network::FilterStatus LdapFilter::onWrite(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(trace, "ldap_proxy: received {} bytes from upstream, end_stream={}",
                 read_callbacks_->connection(), data.length(), end_stream);

  if (state_ != FilterState::NegotiatingUpstream) {
    return Network::FilterStatus::Continue;
  }

  if (end_stream) {
    ENVOY_CONN_LOG(error, "ldap_proxy: upstream closed connection during StartTLS negotiation",
                   read_callbacks_->connection());
    stats_.decoder_error_.inc();
    closeConnection();
    return Network::FilterStatus::StopIteration;
  }

  if (upstream_buffer_.length() + data.length() > kMaxUpstreamBufferSize) {
    ENVOY_CONN_LOG(error, "ldap_proxy: upstream response exceeded max size ({})",
                   read_callbacks_->connection(), kMaxUpstreamBufferSize);
    stats_.decoder_error_.inc();
    closeConnection();
    return Network::FilterStatus::StopIteration;
  }

  upstream_buffer_.move(data);

  if (handleUpstreamStartTlsResponse(upstream_buffer_)) {
    upstream_buffer_.drain(upstream_buffer_.length());
    return Network::FilterStatus::StopIteration;
  }

  return Network::FilterStatus::StopIteration;
}

void LdapFilter::onStartTlsRequest(uint32_t msg_id, size_t /*msg_length*/) {
  ENVOY_CONN_LOG(debug, "ldap_proxy: StartTLS request detected, msgID={}", 
                 read_callbacks_->connection(), msg_id);

  pending_starttls_msg_id_ = msg_id;
  state_ = FilterState::NegotiatingUpstream;
  read_callbacks_->connection().readDisable(true);
  sendUpstreamStartTlsRequest();
}

void LdapFilter::onPlaintextOperation(uint32_t msg_id) {
  ENVOY_CONN_LOG(trace, "ldap_proxy: plaintext operation callback, msgID={}", 
                 read_callbacks_->connection(), msg_id);
}

void LdapFilter::onDecoderError() {
  ENVOY_CONN_LOG(debug, "ldap_proxy: decoder error", read_callbacks_->connection());
  stats_.decoder_error_.inc();
  closeConnection();
}

void LdapFilter::onPipelineViolation() {
  ENVOY_CONN_LOG(debug, "ldap_proxy: pipeline violation detected", read_callbacks_->connection());
  stats_.protocol_violation_.inc();
  closeConnection();
}

void LdapFilter::closeConnection() {
  if (state_ == FilterState::Closed) {
    return;
  }

  state_ = FilterState::Closed;
  decoder_->setClosed();

  pending_downstream_tls_switch_ = false;
  pending_response_bytes_ = 0;
  if (downstream_flush_timer_) {
    downstream_flush_timer_->disableTimer();
  }
  if (upstream_starttls_timer_) {
    upstream_starttls_timer_->disableTimer();
  }

  inspect_buffer_.drain(inspect_buffer_.length());
  upstream_buffer_.drain(upstream_buffer_.length());
  pending_downstream_data_.drain(pending_downstream_data_.length());

  read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

void LdapFilter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    absl::string_view failure_reason = read_callbacks_->connection().transportFailureReason();

    if (!failure_reason.empty()) {
      ENVOY_CONN_LOG(debug, "ldap_proxy: connection closed with transport failure: {}",
                     read_callbacks_->connection(), failure_reason);
      stats_.decoder_error_.inc();
    } else {
      ENVOY_CONN_LOG(debug, "ldap_proxy: connection closed (event={})",
                     read_callbacks_->connection(), static_cast<int>(event));
    }

    if (state_ != FilterState::Closed) {
      state_ = FilterState::Closed;
      decoder_->setClosed();
      if (downstream_flush_timer_) {
        downstream_flush_timer_->disableTimer();
      }
      if (upstream_starttls_timer_) {
        upstream_starttls_timer_->disableTimer();
      }
    }
  }
}

void LdapFilter::sendStartTlsSuccessResponse() {
  auto response = createStartTlsSuccessResponse(pending_starttls_msg_id_);

  Buffer::OwnedImpl response_buffer;
  response_buffer.add(response.data(), response.size());

  ENVOY_CONN_LOG(debug, "ldap_proxy: sending StartTLS success to downstream, msgID={}",
                 read_callbacks_->connection(), pending_starttls_msg_id_);

  read_callbacks_->connection().write(response_buffer, false);
}

void LdapFilter::sendUpstreamStartTlsRequest() {
  auto request = createStartTlsRequest(upstream_starttls_msg_id_);

  Buffer::OwnedImpl request_buffer;
  request_buffer.add(request.data(), request.size());

  ENVOY_CONN_LOG(debug, "ldap_proxy: sending StartTLS request to upstream, msgID={}",
                 read_callbacks_->connection(), upstream_starttls_msg_id_);

  read_callbacks_->injectReadDataToFilterChain(request_buffer, false);
  inspect_buffer_.drain(inspect_buffer_.length());

  if (upstream_starttls_timer_) {
    upstream_starttls_timer_->disableTimer();
  }
  upstream_starttls_timer_ = read_callbacks_->connection().dispatcher().createTimer(
      [this]() {
        if (state_ == FilterState::Closed) {
          return;
        }
        ENVOY_CONN_LOG(error, "ldap_proxy: upstream StartTLS response timeout",
                       read_callbacks_->connection());
        stats_.decoder_error_.inc();
        closeConnection();
      });
  upstream_starttls_timer_->enableTimer(kUpstreamStartTlsTimeout);
}

bool LdapFilter::handleUpstreamStartTlsResponse(Buffer::Instance& data) {
  if (data.length() < 10) {
    ENVOY_CONN_LOG(trace, "ldap_proxy: need more data for upstream response",
                   read_callbacks_->connection());
    return false;
  }

  std::vector<uint8_t> buf(data.length());
  data.copyOut(0, data.length(), buf.data());

  uint8_t result_code = 0;
  uint32_t msg_id = 0;

  if (!parseExtendedResponse(buf.data(), buf.size(), result_code, msg_id)) {
    ENVOY_CONN_LOG(error, "ldap_proxy: failed to parse upstream StartTLS response",
                   read_callbacks_->connection());
    stats_.decoder_error_.inc();
    closeConnection();
    return true;
  }

  ENVOY_CONN_LOG(debug, "ldap_proxy: upstream StartTLS response: msgID={}, resultCode={}",
                 read_callbacks_->connection(), msg_id, result_code);

  if (msg_id != upstream_starttls_msg_id_) {
    ENVOY_CONN_LOG(error, "ldap_proxy: upstream response msgID mismatch: expected={}, got={}",
                   read_callbacks_->connection(), upstream_starttls_msg_id_, msg_id);
    stats_.decoder_error_.inc();
    closeConnection();
    return true;
  }

  if (result_code != kLdapResultSuccess) {
    ENVOY_CONN_LOG(error, "ldap_proxy: upstream rejected StartTLS, resultCode={}",
                   read_callbacks_->connection(), result_code);
    stats_.decoder_error_.inc();
    closeConnection();
    return true;
  }

  ENVOY_CONN_LOG(debug, "ldap_proxy: upstream accepted StartTLS, proceeding with TLS switch",
                 read_callbacks_->connection());

  if (upstream_starttls_timer_) {
    upstream_starttls_timer_->disableTimer();
  }

  if (upstream_starttls_) {
    completeUpstreamOnlyTlsSwitch();
  } else {
    state_ = FilterState::NegotiatingDownstream;
    completeTlsSwitch();
  }
  return true;
}

void LdapFilter::completeTlsSwitch() {
  ENVOY_CONN_LOG(debug, "ldap_proxy: completing TLS switch",
                 read_callbacks_->connection());

  if (!read_callbacks_->startUpstreamSecureTransport()) {
    ENVOY_CONN_LOG(error, "ldap_proxy: failed to start upstream secure transport",
                   read_callbacks_->connection());
    stats_.decoder_error_.inc();
    closeConnection();
    return;
  }

  ENVOY_CONN_LOG(debug, "ldap_proxy: upstream TLS initiated",
                 read_callbacks_->connection());

  Buffer::OwnedImpl empty_buffer;
  read_callbacks_->injectReadDataToFilterChain(empty_buffer, false);

  auto response = createStartTlsSuccessResponse(pending_starttls_msg_id_);
  const uint64_t response_size = response.size();

  Buffer::OwnedImpl response_buffer;
  response_buffer.add(response.data(), response.size());

  ENVOY_CONN_LOG(debug, "ldap_proxy: sending StartTLS success to downstream, msgID={}, size={}",
                 read_callbacks_->connection(), pending_starttls_msg_id_, response_size);

  read_callbacks_->connection().write(response_buffer, false);

  pending_downstream_tls_switch_ = true;
  pending_response_bytes_ = response_size;

  ENVOY_CONN_LOG(debug, "ldap_proxy: waiting for {} bytes to be flushed before downstream TLS",
                 read_callbacks_->connection(), pending_response_bytes_);

  if (!downstream_flush_timer_) {
    downstream_flush_timer_ = read_callbacks_->connection().dispatcher().createTimer([this]() {
      if (state_ == FilterState::Closed || !pending_downstream_tls_switch_) {
        return;
      }
      ENVOY_CONN_LOG(error,
                     "ldap_proxy: downstream StartTLS response flush timed out, closing connection",
                     read_callbacks_->connection());
      closeConnection();
    });
  }
  downstream_flush_timer_->enableTimer(std::chrono::seconds(5));

  read_callbacks_->connection().addBytesSentCallback([this](uint64_t bytes_sent) {
    if (state_ == FilterState::Closed || !pending_downstream_tls_switch_) {
      return true;
    }

    ENVOY_CONN_LOG(trace, "ldap_proxy: {} bytes sent to downstream",
                   read_callbacks_->connection(), bytes_sent);

    if (bytes_sent >= pending_response_bytes_) {
      ENVOY_CONN_LOG(debug, "ldap_proxy: response flushed, switching downstream to TLS",
                     read_callbacks_->connection());
      pending_response_bytes_ = 0;
      pending_downstream_tls_switch_ = false;
      if (downstream_flush_timer_) {
        downstream_flush_timer_->disableTimer();
      }

      if (state_ == FilterState::Closed) {
        return true;
      }

      if (!read_callbacks_->connection().startSecureTransport()) {
        ENVOY_CONN_LOG(error, "ldap_proxy: failed to start downstream secure transport",
                       read_callbacks_->connection());
        stats_.decoder_error_.inc();
        closeConnection();
        return true;
      }

      ENVOY_CONN_LOG(debug, "ldap_proxy: downstream TLS initiated",
                     read_callbacks_->connection());

      transitionToEncrypted();
      return true;
    }

    pending_response_bytes_ -= bytes_sent;
    return false;
  });
}

void LdapFilter::transitionToEncrypted() {
  ENVOY_CONN_LOG(info, "ldap_proxy: StartTLS complete - connection(s) now encrypted",
                 read_callbacks_->connection());

  state_ = FilterState::Passthrough;
  decoder_->setEncrypted();

  if (upstream_starttls_timer_) {
    upstream_starttls_timer_->disableTimer();
  }
  if (downstream_flush_timer_) {
    downstream_flush_timer_->disableTimer();
  }

  read_callbacks_->connection().readDisable(false);
  stats_.starttls_success_.inc();

  pending_starttls_msg_id_ = 0;
  upstream_starttls_msg_id_ = kUpstreamStartTlsMsgId;
  inspect_buffer_.drain(inspect_buffer_.length());
  upstream_buffer_.drain(upstream_buffer_.length());
  pending_downstream_data_.drain(pending_downstream_data_.length());
}

void LdapFilter::initiateUpstreamStartTls() {
  ENVOY_CONN_LOG(debug, "ldap_proxy: initiating proactive upstream StartTLS",
                 read_callbacks_->connection());

  state_ = FilterState::NegotiatingUpstream;
  read_callbacks_->connection().readDisable(true);
  sendUpstreamStartTlsRequest();
}

void LdapFilter::completeUpstreamOnlyTlsSwitch() {
  ENVOY_CONN_LOG(debug, "ldap_proxy: completing upstream-only TLS switch",
                 read_callbacks_->connection());

  if (!read_callbacks_->startUpstreamSecureTransport()) {
    ENVOY_CONN_LOG(error, "ldap_proxy: failed to start upstream secure transport",
                   read_callbacks_->connection());
    stats_.decoder_error_.inc();
    closeConnection();
    return;
  }

  ENVOY_CONN_LOG(debug, "ldap_proxy: upstream TLS initiated",
                 read_callbacks_->connection());

  Buffer::OwnedImpl empty_buffer;
  read_callbacks_->injectReadDataToFilterChain(empty_buffer, false);

  if (pending_downstream_data_.length() > 0) {
    ENVOY_CONN_LOG(debug, "ldap_proxy: forwarding {} bytes of buffered downstream data",
                   read_callbacks_->connection(), pending_downstream_data_.length());
    read_callbacks_->injectReadDataToFilterChain(pending_downstream_data_, false);
    pending_downstream_data_.drain(pending_downstream_data_.length());
  }

  state_ = FilterState::Passthrough;
  decoder_->setEncrypted();

  if (upstream_starttls_timer_) {
    upstream_starttls_timer_->disableTimer();
  }

  read_callbacks_->connection().readDisable(false);
  stats_.starttls_success_.inc();

  inspect_buffer_.drain(inspect_buffer_.length());
  upstream_buffer_.drain(upstream_buffer_.length());

  ENVOY_CONN_LOG(info, "ldap_proxy: upstream TLS complete - downstream plaintext, upstream TLS",
                 read_callbacks_->connection());
}

} // namespace LdapProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
