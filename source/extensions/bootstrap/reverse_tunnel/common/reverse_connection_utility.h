#pragma once

#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseConnectionUtility : public Logger::Loggable<Logger::Id::connection> {
public:
  static constexpr absl::string_view PING_MESSAGE = "RPING";
  static constexpr absl::string_view PROXY_MESSAGE = "PROXY";
  static constexpr absl::string_view DEFAULT_REVERSE_TUNNEL_REQUEST_PATH =
      "/reverse_connections/request";

  static bool isPingMessage(absl::string_view data);

  static Buffer::InstancePtr createPingResponse();

  static bool sendPingResponse(Network::Connection& connection);

  static Api::IoCallUint64Result sendPingResponse(Network::IoHandle& io_handle);

  static bool handlePingMessage(absl::string_view data, Network::Connection& connection);

  static bool extractPingFromHttpData(absl::string_view http_data);

private:
  ReverseConnectionUtility() = delete;
};

// Header names used by reverse tunnel handshake over HTTP.
inline const Http::LowerCaseString& reverseTunnelNodeIdHeader() {
  static const Http::LowerCaseString kHeader{
      absl::StrCat(Http::Headers::get().prefix(), "-reverse-tunnel-node-id")};
  return kHeader;
}

inline const Http::LowerCaseString& reverseTunnelClusterIdHeader() {
  static const Http::LowerCaseString kHeader{
      absl::StrCat(Http::Headers::get().prefix(), "-reverse-tunnel-cluster-id")};
  return kHeader;
}

inline const Http::LowerCaseString& reverseTunnelTenantIdHeader() {
  static const Http::LowerCaseString kHeader{
      absl::StrCat(Http::Headers::get().prefix(), "-reverse-tunnel-tenant-id")};
  return kHeader;
}

class ReverseConnectionMessageHandlerFactory {
public:
  static std::shared_ptr<class PingMessageHandler> createPingHandler();
};

class PingMessageHandler : public std::enable_shared_from_this<PingMessageHandler>,
                           public Logger::Loggable<Logger::Id::connection> {
public:
  PingMessageHandler() = default;
  ~PingMessageHandler() = default;

  bool processPingMessage(absl::string_view data, Network::Connection& connection);

  uint64_t getPingCount() const { return ping_count_; }

private:
  uint64_t ping_count_{0};
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
