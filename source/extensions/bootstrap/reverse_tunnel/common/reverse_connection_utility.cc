#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

bool ReverseConnectionUtility::isPingMessage(absl::string_view data) {
  if (data.empty()) {
    return false;
  }
  return (data.length() == PING_MESSAGE.length() &&
          !memcmp(data.data(), PING_MESSAGE.data(), PING_MESSAGE.length()));
}

Buffer::InstancePtr ReverseConnectionUtility::createPingResponse() {
  return std::make_unique<Buffer::OwnedImpl>(PING_MESSAGE);
}

bool ReverseConnectionUtility::sendPingResponse(Network::Connection& connection) {
  auto ping_buffer = createPingResponse();
  connection.write(*ping_buffer, false);
  ENVOY_LOG(trace, "Reverse connection utility: sent RPING response on connection {}.",
            connection.id());
  return true;
}

Api::IoCallUint64Result ReverseConnectionUtility::sendPingResponse(Network::IoHandle& io_handle) {
  auto ping_buffer = createPingResponse();
  Api::IoCallUint64Result result = io_handle.write(*ping_buffer);
  if (result.ok()) {
    ENVOY_LOG(trace, "Reverse connection utility: sent RPING response. bytes: {}.",
              result.return_value_);
  } else {
    ENVOY_LOG(trace, "Reverse connection utility: failed to send RPING response. error: {}.",
              result.err_->getErrorDetails());
  }
  return result;
}

bool ReverseConnectionUtility::handlePingMessage(absl::string_view data,
                                                 Network::Connection& connection) {
  if (!isPingMessage(data)) {
    return false;
  }
  ENVOY_LOG(trace, "Reverse connection utility: received RPING on connection {}; echoing back.",
            connection.id());
  return sendPingResponse(connection);
}

bool ReverseConnectionUtility::extractPingFromHttpData(absl::string_view http_data) {
  if (http_data.find(PING_MESSAGE) != absl::string_view::npos) {
    ENVOY_LOG(trace, "Reverse connection utility: found RPING in HTTP data.");
    return true;
  }
  return false;
}

std::shared_ptr<PingMessageHandler> ReverseConnectionMessageHandlerFactory::createPingHandler() {
  return std::make_shared<PingMessageHandler>();
}

bool PingMessageHandler::processPingMessage(absl::string_view data,
                                            Network::Connection& connection) {
  if (ReverseConnectionUtility::isPingMessage(data)) {
    ++ping_count_;
    ENVOY_LOG(debug, "Ping handler: processing ping #{} on connection {}", ping_count_,
              connection.id());
    return ReverseConnectionUtility::sendPingResponse(connection);
  }
  return false;
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
