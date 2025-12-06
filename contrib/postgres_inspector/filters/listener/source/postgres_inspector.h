#pragma once

#include <memory>
#include <string>

#include "envoy/event/timer.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"

#include "contrib/envoy/extensions/filters/listener/postgres_inspector/v3alpha/postgres_inspector.pb.h"
#include "contrib/postgres_inspector/filters/listener/source/postgres_message_parser.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace PostgresInspector {

using ProtoConfig =
    envoy::extensions::filters::listener::postgres_inspector::v3alpha::PostgresInspector;

/**
 * All stats for the Postgres inspector. @see stats_macros.h
 */
#define ALL_POSTGRES_INSPECTOR_STATS(COUNTER, HISTOGRAM)                                           \
  COUNTER(postgres_found)                                                                          \
  COUNTER(postgres_not_found)                                                                      \
  COUNTER(ssl_requested)                                                                           \
  COUNTER(ssl_not_requested)                                                                       \
  COUNTER(startup_message_too_large)                                                               \
  COUNTER(startup_message_timeout)                                                                 \
  COUNTER(protocol_error)                                                                          \
  HISTOGRAM(bytes_processed, Bytes)

/**
 * Definition of all stats for the Postgres inspector. @see stats_macros.h
 */
struct PostgresInspectorStats {
  ALL_POSTGRES_INSPECTOR_STATS(GENERATE_COUNTER_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

enum class ParseState {
  // Waiting for initial message (SSL request, cancel request, or startup message).
  Initial,
  // Parse result is out. It could be Postgres or not.
  Done,
  // Parser reports unrecoverable error.
  Error
};

/**
 * Global configuration for Postgres inspector.
 */
class Config {
public:
  Config(Stats::Scope& scope, const ProtoConfig& proto_config);

  const PostgresInspectorStats& stats() const { return stats_; }
  bool enableMetadataExtraction() const { return enable_metadata_extraction_; }
  uint32_t maxStartupMessageSize() const { return max_startup_message_size_; }
  std::chrono::milliseconds startupTimeout() const { return startup_timeout_; }

  // Maximum startup message size (10KB per PostgreSQL definition) by default.
  // PostgreSQL defines MAX_STARTUP_PACKET_LENGTH as 10000 bytes.
  static constexpr uint32_t DEFAULT_MAX_STARTUP_MESSAGE_SIZE = 10000;
  // Default timeout for startup message (10 seconds).
  static constexpr uint32_t DEFAULT_STARTUP_TIMEOUT_MS = 10000;

private:
  PostgresInspectorStats stats_;
  const bool enable_metadata_extraction_;
  const uint32_t max_startup_message_size_;
  const std::chrono::milliseconds startup_timeout_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

/**
 * Postgres inspector listener filter.
 */
class Filter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const ConfigSharedPtr& config);

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;
  Network::FilterStatus onData(Network::ListenerFilterBuffer& buffer) override;
  size_t maxReadBytes() const override { return config_->maxStartupMessageSize(); }

private:
  Network::FilterStatus processInitialData(Network::ListenerFilterBuffer& buffer);
  Network::FilterStatus processStartupMessage(Network::ListenerFilterBuffer& buffer);

  void extractMetadata(const StartupMessage& message);
  void onTimeout();
  void done(bool success);

  ConfigSharedPtr config_;
  Network::ListenerFilterCallbacks* cb_{nullptr};
  ParseState state_{ParseState::Initial};
  uint64_t bytes_read_{0};
  uint64_t bytes_processed_for_histogram_{0};
  bool ssl_requested_{false};
  Event::TimerPtr timeout_timer_;

  // Extracted metadata.
  std::string database_;
  std::string user_;
  std::string application_name_;
};

} // namespace PostgresInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
