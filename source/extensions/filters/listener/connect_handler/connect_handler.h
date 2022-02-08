#pragma once

#include <http_parser.h>

#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ConnectHandler {

/**
 * All stats for the connect handler. @see stats_macros.h
 */
#define ALL_CONNECT_HANDLER_STATS(COUNTER)                                                         \
  COUNTER(read_error)                                                                              \
  COUNTER(write_error)                                                                             \
  COUNTER(connect_not_found)                                                                       \
  COUNTER(connect_found)

/**
 * Definition of all stats for the connect handler. @see stats_macros.h
 */
struct ConnectHandlerStats {
  ALL_CONNECT_HANDLER_STATS(GENERATE_COUNTER_STRUCT)
};

enum class ParseState {
  // Parse result is out. It could be HTTP CONNECT or not.
  Done,
  // Parser expects more data.
  Continue,
  // Parser reports unrecoverable error.
  Error
};

/**
 * Global configuration for CONNECT handler.
 */
class Config {
public:
  Config(Stats::Scope& scope);

  const ConnectHandlerStats& stats() const { return stats_; }

  static constexpr uint32_t MAX_INSPECT_SIZE = 8192;

private:
  ConnectHandlerStats stats_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

/**
 * CONNECT handler listener filter.
 */
class Filter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const ConfigSharedPtr config);

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;

private:
  ParseState parseConnect(uint8_t* buf, size_t len);
  ParseState onRead();

  ConfigSharedPtr config_;
  Network::ListenerFilterCallbacks* cb_{};

  // Use static thread_local to avoid allocating buffer over and over again.
  static thread_local uint8_t buf_[Config::MAX_INSPECT_SIZE];
  static constexpr absl::string_view HTTP1_CONNECT_PREFACE = "CONNECT";
  Buffer::OwnedImpl resp_buf_{};
  http_parser parser_;
};

} // namespace ConnectHandler
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
