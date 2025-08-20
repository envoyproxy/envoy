#include "contrib/postgres_inspector/filters/listener/source/postgres_inspector.h"

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
  ENVOY_LOG(debug, "postgres inspector: onData called with {} bytes, bytes_read_={}, state_={}",
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
    // After processInitialData we may have drained 8 bytes. We recompute bytes_read_.
    bytes_read_ = buffer.rawSlice().len_;
    return status;
  }
  case ParseState::ProcessingStartup: {
    auto status = processStartupMessage(buffer);
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

  ENVOY_LOG(debug, "postgres inspector: processInitialData called with len={}", len);

  // We need at least 8 bytes for initial message.
  if (len < 8) {
    ENVOY_LOG(debug, "postgres inspector: insufficient data ({} < 8)", len);
    return Network::FilterStatus::StopIteration;
  }

  // Create a temporary buffer view for parsing.
  Buffer::OwnedImpl temp_buffer;
  temp_buffer.add(raw_slice.mem_, len);

  ENVOY_LOG(debug, "postgres inspector: created temp buffer with {} bytes", temp_buffer.length());

  // Log the raw data for debugging purposes.
  if (len >= 8) {
    const uint32_t* data = static_cast<const uint32_t*>(raw_slice.mem_);
    ENVOY_LOG(debug, "postgres inspector: raw data - word1=0x{:08x} word2=0x{:08x}", data[0],
              data[1]);
  }

  // Check if it's an SSL request.
  bool is_ssl = PostgresMessageParser::isSslRequest(temp_buffer, 0);
  ENVOY_LOG(debug, "postgres inspector: isSslRequest returned {}", is_ssl);
  if (is_ssl) {
    ENVOY_LOG(debug, "postgres inspector: SSL request detected");
    config_->stats().ssl_requested_.inc();
    ssl_requested_ = true;

    // Set protocol as Postgres.
    cb_->socket().setDetectedTransportProtocol("postgres");
    config_->stats().postgres_found_.inc();

    // Pass through to TLS Inspector. Drain the SSLRequest bytes so TLS inspector sees
    // ClientHello.
    if (len >= 8) {
      const bool drained = buffer.drain(8);
      ENVOY_LOG(debug, "postgres inspector: drained SSLRequest bytes: {}", drained);
    }
    ENVOY_LOG(debug, "postgres inspector: passing SSL request to TLS inspector");
    done(true);
    return Network::FilterStatus::Continue;
  }

  // Not SSL request, should be startup message.
  ENVOY_LOG(trace, "postgres inspector: no SSL request, checking for startup message");
  state_ = ParseState::ProcessingStartup;
  return processStartupMessage(buffer);
}

Network::FilterStatus Filter::processSslResponse(Network::ListenerFilterBuffer&) {
  // SSL negotiation is always passed through to TLS inspector.
  return Network::FilterStatus::Continue;
}

Network::FilterStatus Filter::processStartupMessage(Network::ListenerFilterBuffer& buffer) {
  const auto raw_slice = buffer.rawSlice();
  const size_t len = raw_slice.len_;

  // Create a temporary buffer view for parsing.
  Buffer::OwnedImpl temp_buffer;
  temp_buffer.add(raw_slice.mem_, len);

  StartupMessage message;

  if (!PostgresMessageParser::parseStartupMessage(temp_buffer, 0, message,
                                                  config_->maxStartupMessageSize())) {
    // Check if message is too large.
    if (len >= config_->maxStartupMessageSize()) {
      ENVOY_LOG(debug, "postgres inspector: startup message too large: {} bytes", len);
      config_->stats().startup_message_too_large_.inc();
      state_ = ParseState::Error;
      cb_->socket().ioHandle().close();
      return Network::FilterStatus::StopIteration;
    }

    // Check if the claimed message length looks unreasonable. In which case,
    // it is likely not Postgres.
    uint32_t claimed_length = 0;
    if (len >= 4) {
      claimed_length = temp_buffer.peekBEInt<uint32_t>(0);
      // If claimed length is way larger than what we have and seems unreasonable, reject
      if (claimed_length > len * 10 && claimed_length > 1000) {
        ENVOY_LOG(debug, "postgres inspector: unreasonable message length {}, not Postgres",
                  claimed_length);
        config_->stats().protocol_error_.inc();
        done(false);
        return Network::FilterStatus::Continue;
      }
    }

    // Need more data.
    ENVOY_LOG(trace, "postgres inspector: need more data for startup message");
    return Network::FilterStatus::StopIteration;
  }

  // Validate protocol version.
  if (message.protocol_version != POSTGRES_PROTOCOL_VERSION) {
    ENVOY_LOG(debug, "postgres inspector: invalid protocol version {}", message.protocol_version);
    config_->stats().protocol_error_.inc();
    config_->stats().postgres_not_found_.inc();
    done(false);
    return Network::FilterStatus::Continue;
  }

  // Valid Postgres connection.
  ENVOY_LOG(debug, "postgres inspector: valid Postgres connection detected");
  cb_->socket().setDetectedTransportProtocol("postgres");
  config_->stats().postgres_found_.inc();

  if (!ssl_requested_) {
    config_->stats().ssl_not_requested_.inc();
  }

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
  ENVOY_LOG(debug, "postgres inspector: timeout waiting for startup message");
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
  config_->stats().bytes_processed_.recordValue(bytes_read_);

  if (!success) {
    config_->stats().postgres_not_found_.inc();
  }

  ENVOY_LOG(trace, "postgres inspector: inspection complete, success: {}", success);
}

} // namespace PostgresInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
