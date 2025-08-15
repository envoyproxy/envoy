#pragma once

#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Utility class for reverse connection ping/heartbeat functionality.
 */
class ReverseConnectionUtility : public Logger::Loggable<Logger::Id::connection> {
public:
  // Constants following Envoy naming conventions.
  static constexpr absl::string_view PING_MESSAGE = "RPING";
  static constexpr absl::string_view PROXY_MESSAGE = "PROXY";

  /**
   * Check if received data contains a ping message.
   * @param data the received data to check.
   * @return true if data contains RPING message.
   */
  static bool isPingMessage(absl::string_view data);

  /**
   * Create a ping response buffer.
   * @return Buffer containing RPING response.
   */
  static Buffer::InstancePtr createPingResponse();

  /**
   * Send ping response using connection's IO handle.
   * @param connection the connection to send ping response on.
   * @return true if ping was sent successfully.
   */
  static bool sendPingResponse(Network::Connection& connection);

  /**
   * Send ping response using raw IO handle.
   * @param io_handle the IO handle to write to.
   * @return Api::IoCallUint64Result the write result.
   */
  static Api::IoCallUint64Result sendPingResponse(Network::IoHandle& io_handle);

  /**
   * Handle ping message detection and response.
   * @param data the incoming data buffer.
   * @param connection the connection to respond on.
   * @return true if data was a ping message and was handled.
   */
  static bool handlePingMessage(absl::string_view data, Network::Connection& connection);

  /**
   * Extract ping message from HTTP-embedded content.
   * @param http_data the HTTP response data.
   * @return true if RPING was found and extracted.
   */
  static bool extractPingFromHttpData(absl::string_view http_data);

private:
  ReverseConnectionUtility() = delete;
};

/**
 * Factory for creating reverse connection message handlers.
 */
class ReverseConnectionMessageHandlerFactory {
public:
  /**
   * Create a shared ping handler instance.
   * @return shared_ptr to ping handler.
   */
  static std::shared_ptr<class PingMessageHandler> createPingHandler();
};

/**
 * Ping message handler that can be shared across filters.
 */
class PingMessageHandler : public std::enable_shared_from_this<PingMessageHandler>,
                           public Logger::Loggable<Logger::Id::connection> {
public:
  PingMessageHandler() = default;
  ~PingMessageHandler() = default;

  /**
   * Process incoming data for ping messages.
   * @param data incoming data.
   * @param connection connection to respond on.
   * @return true if ping was handled.
   */
  bool processPingMessage(absl::string_view data, Network::Connection& connection);

  /**
   * Get ping message statistics.
   * @return number of pings processed.
   */
  uint64_t getPingCount() const { return ping_count_; }

private:
  uint64_t ping_count_{0};
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
