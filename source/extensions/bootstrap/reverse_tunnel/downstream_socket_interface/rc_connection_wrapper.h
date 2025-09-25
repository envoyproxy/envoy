#pragma once

#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/network/filter_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declaration.
class ReverseConnectionIOHandle;

/**
 * Simple read filter for handling reverse connection handshake responses.
 * This filter processes the HTTP response from the upstream server during handshake.
 */
class SimpleConnReadFilter : public Network::ReadFilterBaseImpl,
                             public Logger::Loggable<Logger::Id::main> {
public:
  /**
   * Constructor that stores pointer to parent wrapper.
   */
  explicit SimpleConnReadFilter(void* parent) : parent_(parent) {}

  // Network::ReadFilter overrides
  Network::FilterStatus onData(Buffer::Instance& buffer, bool end_stream) override;

private:
  void* parent_; // Pointer to RCConnectionWrapper to avoid circular dependency.
};

/**
 * Wrapper for reverse connections that manages the connection lifecycle and handshake.
 * It handles the handshake process (both gRPC and HTTP fallback) and manages connection
 * callbacks and cleanup.
 */
class RCConnectionWrapper : public Network::ConnectionCallbacks,
                            public Event::DeferredDeletable,
                            public Logger::Loggable<Logger::Id::main>,
                            public Http::ResponseDecoder,
                            public Http::ConnectionCallbacks {
  friend class SimpleConnReadFilterTest;

public:
  /**
   * Constructor for RCConnectionWrapper.
   * @param parent reference to the parent ReverseConnectionIOHandle
   * @param connection the client connection to wrap
   * @param host the upstream host description
   * @param cluster_name the name of the cluster
   */
  RCConnectionWrapper(ReverseConnectionIOHandle& parent, Network::ClientConnectionPtr connection,
                      Upstream::HostDescriptionConstSharedPtr host,
                      const std::string& cluster_name);

  /**
   * Destructor for RCConnectionWrapper.
   * Performs defensive cleanup to prevent crashes during shutdown.
   */
  ~RCConnectionWrapper() override;

  // Network::ConnectionCallbacks overrides
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // Http::ResponseDecoder overrides
  void decode1xxHeaders(Http::ResponseHeaderMapPtr&&) override {}
  void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(Buffer::Instance&, bool) override {}
  void decodeTrailers(Http::ResponseTrailerMapPtr&&) override {}
  void decodeMetadata(Http::MetadataMapPtr&&) override {}
  void dumpState(std::ostream&, int) const override {}

  // Http::ConnectionCallbacks overrides
  void onGoAway(Http::GoAwayErrorCode) override {}
  void onSettings(Http::ReceivedSettings&) override {}
  void onMaxStreamsChanged(uint32_t) override {}

  /**
   * Initiate the reverse connection handshake (HTTP only).
   * @param src_tenant_id the tenant identifier
   * @param src_cluster_id the cluster identifier
   * @param src_node_id the node identifier
   * @return the local address as string
   */
  std::string connect(const std::string& src_tenant_id, const std::string& src_cluster_id,
                      const std::string& src_node_id);

  /**
   * Release ownership of the connection.
   * @return the connection pointer (ownership transferred to caller)
   */
  Network::ClientConnectionPtr releaseConnection() { return std::move(connection_); }

  /**
   * Process HTTP response from upstream.
   * @param buffer the response data
   * @param end_stream whether this is the end of the stream
   */
  void processHttpResponse(Buffer::Instance& buffer, bool end_stream);

  /**
   * Handle successful handshake completion.
   */
  void onHandshakeSuccess();

  /**
   * Handle handshake failure.
   * @param message error message
   */
  void onHandshakeFailure(const std::string& message);

  /**
   * Perform graceful shutdown of the connection.
   */
  void shutdown();

  /**
   * Get the underlying connection.
   * @return pointer to the client connection
   */
  Network::ClientConnection* getConnection() { return connection_.get(); }

  /**
   * Get the host description.
   * @return shared pointer to the host description
   */
  Upstream::HostDescriptionConstSharedPtr getHost() { return host_; }

private:
  ReverseConnectionIOHandle& parent_;
  Network::ClientConnectionPtr connection_;
  Upstream::HostDescriptionConstSharedPtr host_;
  std::string cluster_name_;
  std::string connection_key_;
  bool http_handshake_sent_{false};
  bool handshake_completed_{false};
  bool shutdown_called_{false};

public:
  // Dispatch incoming bytes to HTTP/1 codec.
  void dispatchHttp1(Buffer::Instance& buffer);

private:
  // HTTP/1 codec used to send request and parse response.
  std::unique_ptr<Http::Http1::ClientConnectionImpl> http1_client_codec_;
  // Base interface pointer used to call dispatch via public API.
  Http::Connection* http1_parse_connection_{nullptr};
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
