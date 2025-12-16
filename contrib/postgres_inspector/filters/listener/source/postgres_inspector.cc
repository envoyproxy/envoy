#include "contrib/postgres_inspector/filters/listener/source/postgres_inspector.h"

#include <algorithm>

#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "contrib/envoy/extensions/filters/listener/postgres_inspector/v3alpha/postgres_inspector.pb.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace PostgresInspector {

Config::Config(Stats::Scope& scope, const ProtoConfig& proto_config)
    : stats_(PostgresInspectorStats{
          ALL_POSTGRES_INSPECTOR_STATS(POOL_COUNTER_PREFIX(scope, "postgres_inspector"),
                                       POOL_HISTOGRAM_PREFIX(scope, "postgres_inspector"))}),
      enable_metadata_extraction_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, enable_metadata_extraction, true)),
      max_startup_message_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          proto_config, max_startup_message_size, DEFAULT_MAX_STARTUP_MESSAGE_SIZE)),
      startup_timeout_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(proto_config, startup_timeout, DEFAULT_STARTUP_TIMEOUT_MS))) {}

Filter::Filter(const ConfigSharedPtr& config) : config_(config) {}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(trace, "postgres inspector: new connection accepted");
  cb_ = &cb;

  // Set up timeout timer.
  if (config_->startupTimeout().count() > 0) {
    timeout_timer_ = cb.dispatcher().createTimer([this]() { onTimeout(); });
    timeout_timer_->enableTimer(config_->startupTimeout());
  }

  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus Filter::onData(Network::ListenerFilterBuffer& buffer) {
  const auto raw_slice = buffer.rawSlice();
  ENVOY_LOG(trace, "postgres inspector: onData called with {} bytes, bytes_read_={}, state_={}",
            raw_slice.len_, bytes_read_, static_cast<int>(state_));

  // Skip data we've already processed.
  if (static_cast<uint64_t>(raw_slice.len_) <= bytes_read_) {
    ENVOY_LOG(debug, "postgres inspector: skipping already processed data ({} <= {})",
              raw_slice.len_, bytes_read_);
    return Network::FilterStatus::StopIteration;
  }

  // Update bytes read after processing state logic to account for drains.
  switch (state_) {
  case ParseState::Initial: {
    auto status = processInitialData(buffer);
    // After processInitialData we may have drained bytes. Recompute bytes_read_.
    bytes_read_ = buffer.rawSlice().len_;
    return status;
  }
  case ParseState::Done:
    return Network::FilterStatus::Continue;
  case ParseState::Error:
    cb_->socket().ioHandle().close();
    return Network::FilterStatus::StopIteration;
  }

  IS_ENVOY_BUG("unexpected postgres inspector parse state");
  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus Filter::processInitialData(Network::ListenerFilterBuffer& buffer) {
  const auto raw_slice = buffer.rawSlice();
  const size_t len = raw_slice.len_;

  ENVOY_LOG(trace, "postgres inspector: processInitialData called with len={}", len);

  // We need at least STARTUP_HEADER_SIZE bytes for initial message.
  if (len < STARTUP_HEADER_SIZE) {
    ENVOY_LOG(trace, "postgres inspector: insufficient data ({} < {})", len, STARTUP_HEADER_SIZE);
    return Network::FilterStatus::StopIteration;
  }

  // Create a temporary buffer view for parsing.
  Buffer::OwnedImpl temp_buffer;
  temp_buffer.add(raw_slice.mem_, len);

  ENVOY_LOG(trace, "postgres inspector: created temp buffer with {} bytes", temp_buffer.length());

  // Check if it's an SSL request.
  bool is_ssl = PostgresMessageParser::isSslRequest(temp_buffer, 0);
  ENVOY_LOG(trace, "postgres inspector: isSslRequest returned {}", is_ssl);
  if (is_ssl) {
    ENVOY_LOG(debug, "postgres inspector: SSL request detected");
    ssl_requested_ = true;

    // Set protocol as Postgres.
    cb_->socket().setDetectedTransportProtocol("postgres");
    config_->stats().postgres_found_.inc();
    config_->stats().ssl_requested_.inc();
    bytes_processed_for_histogram_ = SSL_REQUEST_MESSAGE_SIZE;

    // Inspector only detect and mark the protocol. SSL negotiation must be handled by the
    // other filter chain components:
    // 1. The postgres_proxy network filter, OR
    // 2. A starttls transport socket configured in the filter chain.
    //
    // For PostgreSQL 17+: Client may send ClientHello immediately after SSLRequest.
    // For PostgreSQL < 17: Client waits for server response before sending ClientHello.
    // In both cases, proper SSL handling must be configured in the filter chain.

    bytes_read_ = buffer.rawSlice().len_;
    done(true);
    return Network::FilterStatus::Continue;
  }

  // Check if it's a CancelRequest. This is sent by clients to cancel long-running queries.
  // CancelRequest is special because:
  // - It's sent on a NEW TCP connection (separate from the original query connection).
  // - It's always unencrypted, even if the original connection used SSL.
  // - Format: Int32(16) + Int32(80877102) + Int32(process_id) + Int32(secret_key).
  // - It does NOT contain startup parameters (no user/database metadata to extract).
  if (PostgresMessageParser::isCancelRequest(temp_buffer, 0)) {
    ENVOY_LOG(debug, "postgres inspector: CancelRequest detected.");
    // Treat as Postgres protocol present but no SSL requested.
    cb_->socket().setDetectedTransportProtocol("postgres");
    config_->stats().postgres_found_.inc();
    config_->stats().ssl_not_requested_.inc();
    bytes_processed_for_histogram_ = CANCEL_REQUEST_MESSAGE_SIZE;
    bytes_read_ = buffer.rawSlice().len_;
    done(true);
    return Network::FilterStatus::Continue;
  }

  // Not SSL request, try parsing startup message.
  ENVOY_LOG(trace, "postgres inspector: no SSL request, checking for startup message.");
  return processStartupMessage(buffer);
}

Network::FilterStatus Filter::processStartupMessage(Network::ListenerFilterBuffer& buffer) {
  const auto raw_slice = buffer.rawSlice();
  const size_t len = raw_slice.len_;

  // Create a temporary buffer view for parsing.
  Buffer::OwnedImpl temp_buffer;
  temp_buffer.add(raw_slice.mem_, len);

  StartupMessage message;

  const uint32_t max_size = std::min<uint32_t>(config_->maxStartupMessageSize(), 10000);
  if (len < 4) {
    return Network::FilterStatus::StopIteration;
  }
  const uint32_t claimed_length = temp_buffer.peekBEInt<uint32_t>(0);
  const uint32_t maybe_version =
      (len >= STARTUP_HEADER_SIZE) ? temp_buffer.peekBEInt<uint32_t>(4) : 0;
  if (claimed_length > max_size) {
    if (maybe_version == POSTGRES_PROTOCOL_VERSION) {
      ENVOY_LOG(debug, "postgres inspector: startup message too large: {} bytes.", claimed_length);
      config_->stats().startup_message_too_large_.inc();
      state_ = ParseState::Error;
      cb_->socket().ioHandle().close();
      return Network::FilterStatus::StopIteration;
    } else {
      // Not a valid startup header; treat as not Postgres.
      ENVOY_LOG(debug, "postgres inspector: invalid header with excessive length ({}).",
                claimed_length);
      config_->stats().protocol_error_.inc();
      done(false);
      return Network::FilterStatus::Continue;
    }
  }
  if (!PostgresMessageParser::parseStartupMessage(temp_buffer, 0, message, max_size)) {
    // Need more data if the claimed length seems plausible; otherwise treat as not Postgres.
    if (claimed_length >= STARTUP_HEADER_SIZE && claimed_length <= max_size &&
        len < claimed_length) {
      ENVOY_LOG(trace, "postgres inspector: need more data for startup message ({} < {}).", len,
                claimed_length);
      return Network::FilterStatus::StopIteration;
    }
    ENVOY_LOG(debug, "postgres inspector: invalid startup message.");
    config_->stats().protocol_error_.inc();
    done(false);
    return Network::FilterStatus::Continue;
  }

  // Validate protocol version.
  if (message.protocol_version != POSTGRES_PROTOCOL_VERSION) {
    ENVOY_LOG(debug, "postgres inspector: invalid protocol version {}", message.protocol_version);
    config_->stats().protocol_error_.inc();
    done(false);
    return Network::FilterStatus::Continue;
  }

  // Valid Postgres connection.
  ENVOY_LOG(debug, "postgres inspector: valid Postgres connection detected.");
  cb_->socket().setDetectedTransportProtocol("postgres");
  config_->stats().postgres_found_.inc();
  config_->stats().ssl_not_requested_.inc();
  bytes_processed_for_histogram_ = message.length;

  // Extract metadata if enabled.
  if (config_->enableMetadataExtraction()) {
    extractMetadata(message);
  }

  done(true);
  return Network::FilterStatus::Continue;
}

void Filter::extractMetadata(const StartupMessage& message) {
  // Extract key parameters.
  const auto user_it = message.parameters.find("user");
  if (user_it != message.parameters.end()) {
    user_ = user_it->second;
    ENVOY_LOG(trace, "postgres inspector: user={}", user_);
  }

  const auto database_it = message.parameters.find("database");
  if (database_it != message.parameters.end()) {
    database_ = database_it->second;
  } else if (!user_.empty()) {
    // Default database name is same as user.
    database_ = user_;
  }
  ENVOY_LOG(trace, "postgres inspector: database={}", database_);

  const auto app_it = message.parameters.find("application_name");
  if (app_it != message.parameters.end()) {
    application_name_ = app_it->second;
    ENVOY_LOG(trace, "postgres inspector: application_name={}", application_name_);
  }

  // Store metadata as typed metadata in stream info dynamic metadata.
  if (!user_.empty() || !database_.empty() || !application_name_.empty()) {
    envoy::extensions::filters::listener::postgres_inspector::v3alpha::StartupMetadata typed;
    if (!user_.empty()) {
      typed.set_user(user_);
    }
    if (!database_.empty()) {
      typed.set_database(database_);
    }
    if (!application_name_.empty()) {
      typed.set_application_name(application_name_);
    }

    Protobuf::Any any;
    any.PackFrom(typed);
    cb_->setDynamicTypedMetadata("envoy.postgres_inspector", any);
    ENVOY_LOG(debug, "postgres inspector: extracted metadata - user: {}, database: {}, app: {}",
              user_, database_, application_name_);
  }
}

void Filter::onTimeout() {
  ENVOY_LOG(debug, "postgres inspector: timeout waiting for startup message.");
  // Check if we've already completed processing. This can happen if done() was called
  // just before the timeout fired. Since a TCP connection is processed by a single thread,
  // we don't need complex locking - just check the state.
  if (state_ == ParseState::Done) {
    return;
  }
  config_->stats().startup_message_timeout_.inc();
  state_ = ParseState::Error;
  cb_->socket().ioHandle().close();
}

void Filter::done(bool success) {
  if (timeout_timer_) {
    timeout_timer_->disableTimer();
    timeout_timer_.reset();
  }

  state_ = ParseState::Done;
  // Record bytes processed for this inspection.
  config_->stats().bytes_processed_.recordValue(bytes_processed_for_histogram_);

  if (!success) {
    config_->stats().postgres_not_found_.inc();
  }

  ENVOY_LOG(trace, "postgres inspector: inspection complete, success: {}", success);
}

} // namespace PostgresInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
