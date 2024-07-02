#pragma once

#include <openssl/ssl.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/service/tls_session_cache/v3/tls_session_cache.pb.h"

// #include "envoy/stats/scope.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace SessionCache {

class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;

  virtual void onStoreCompletion(const std::string& session_id,
                                 const std::string& session_data) PURE;
  virtual void onFetchCompletion(const std::string& session_id,
                                 const std::string& session_data) PURE;
};

class Client {
public:
  virtual ~Client() = default;

  /**
   * Request to store/fetch a TLS session to/from remote cache.
   * NOTE: It is possible for the completion callback to be called immediately on the same stack
   *       frame. Calling code should account for this.
   * @param callbacks supplies the completion callbacks.
   * @param session_id supplies the session id
   * @param parent_span source for generating an egress child span as part of the trace.
   * @param stream_info supplies the stream info for the request.
   *
   */
  virtual void storeTlsSessionCache(Network::TransportSocketCallbacks* callbacks, SSL* ssl,
                                    int index, const std::string& session_id,
                                    const uint8_t* session_data, std::size_t len) PURE;

  virtual void fetchTlsSessionCache(Network::TransportSocketCallbacks* callbacks, SSL* ssl,
                                    int index, const std::string& session_id, uint8_t* session_data,
                                    std::size_t* len) PURE;
};

using ClientPtr = std::shared_ptr<Client>;

} // namespace SessionCache
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
