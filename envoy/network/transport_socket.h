#pragma once

#include <vector>

#include "envoy/api/io_error.h"
#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/network/io_handle.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/post_io_action.h"
#include "envoy/network/proxy_protocol.h"
#include "envoy/ssl/connection.h"
#include "envoy/ssl/context.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/types/optional.h"

namespace quic {
class QuicCryptoClientConfig;
}

namespace Envoy {

namespace Upstream {
class HostDescription;
}
namespace Ssl {
class ClientContextConfig;
}

namespace Network {

class Connection;
enum class ConnectionEvent;

/**
 * Result of each I/O event.
 */
struct IoResult {
  IoResult(PostIoAction action, uint64_t bytes_processed, bool end_stream_read)
      : action_(action), bytes_processed_(bytes_processed), end_stream_read_(end_stream_read),
        err_code_(absl::nullopt) {}

  IoResult(PostIoAction action, uint64_t bytes_processed, bool end_stream_read,
           absl::optional<Api::IoError::IoErrorCode> err_code)
      : action_(action), bytes_processed_(bytes_processed), end_stream_read_(end_stream_read),
        err_code_(err_code) {}

  PostIoAction action_;

  /**
   * Number of bytes processed by the I/O event.
   */
  uint64_t bytes_processed_;

  /**
   * True if an end-of-stream was read from a connection. This
   * can only be true for read operations.
   */
  bool end_stream_read_;

  /**
   * The underlying I/O error code.
   */
  absl::optional<Api::IoError::IoErrorCode> err_code_;
};

/**
 * Callbacks used by transport socket instances to communicate with connection.
 */
class TransportSocketCallbacks {
public:
  virtual ~TransportSocketCallbacks() = default;

  /**
   * @return reference to the IoHandle associated with the connection.
   */
  virtual IoHandle& ioHandle() PURE;

  /**
   * @return const reference to the IoHandle associated with the connection.
   */
  virtual const IoHandle& ioHandle() const PURE;

  /**
   * @return Network::Connection& the connection interface.
   */
  virtual Network::Connection& connection() PURE;

  /**
   * @return bool whether the read buffer should be drained. This is used to enforce yielding for
   *         configured read limits.
   */
  virtual bool shouldDrainReadBuffer() PURE;

  /**
   * Mark the transport socket as readable in order to force a read in a future iteration of the
   * event loop. This is used when yielding following shouldDrainReadBuffer().
   */
  virtual void setTransportSocketIsReadable() PURE;

  /**
   * Raise a connection event to the connection. This can be used by a secure socket (e.g. TLS)
   * to raise a connected event when handshake is done.
   * @param event supplies the connection event
   */
  virtual void raiseEvent(ConnectionEvent event) PURE;

  /**
   * If the callbacks' write buffer is not empty, try to drain the buffer.
   * As of 2/20, used by Google.
   */
  virtual void flushWriteBuffer() PURE;
};

/**
 * A transport socket that does actual read / write. It can also do some transformations on
 * the data (e.g. TLS).
 */
class TransportSocket {
public:
  virtual ~TransportSocket() = default;

  /**
   * Called by connection once to initialize the transport socket callbacks that the transport
   * socket should use.
   * @param callbacks supplies the callbacks instance.
   */
  virtual void setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) PURE;

  /**
   * @return std::string the protocol to use as selected by network level negotiation. (E.g., ALPN).
   *         If network level negotiation is not supported by the connection or no protocol
   *         has been negotiated the empty string is returned.
   */
  virtual std::string protocol() const PURE;

  /**
   * @return std::string the last failure reason occurred on the transport socket. If no failure
   *         has been occurred the empty string is returned.
   */
  virtual absl::string_view failureReason() const PURE;

  /**
   * @return bool whether the socket can be flushed and closed.
   */
  virtual bool canFlushClose() PURE;

  /**
   * Connect the underlying transport.
   * @param socket provides the socket to connect.
   * @return int the result from connect.
   */
  virtual Api::SysCallIntResult connect(Network::ConnectionSocket& socket) {
    return socket.connect(socket.connectionInfoProvider().remoteAddress());
  }

  /**
   * Closes the transport socket.
   * @param event supplies the connection event that is closing the socket.
   */
  virtual void closeSocket(Network::ConnectionEvent event) PURE;

  /**
   * @param buffer supplies the buffer to read to.
   * @return IoResult the result of the read action.
   */
  virtual IoResult doRead(Buffer::Instance& buffer) PURE;

  /**
   * @param buffer supplies the buffer to write from
   * @param end_stream supplies whether this is the end of the stream. If true and all
   *        data in buffer is written, the connection will be half-closed.
   * @return IoResult the result of the write action.
   */
  virtual IoResult doWrite(Buffer::Instance& buffer, bool end_stream) PURE;

  /**
   * Called when underlying transport is established.
   */
  virtual void onConnected() PURE;

  /**
   * @return the const SSL connection data if this is an SSL connection, or nullptr if it is not.
   */
  virtual Ssl::ConnectionInfoConstSharedPtr ssl() const PURE;

  /**
   * Instructs a transport socket to start using secure transport.
   * It is up to the caller of this method to manage the coordination between the client
   * and server with regard to when to start secure transport. This is typically done via
   * some signal message. See STARTTLS for an example of such negotiation.
   * Note: Not all transport sockets support such operation.
   * @return boolean indicating if the transport socket was able to start secure transport.
   */
  virtual bool startSecureTransport() PURE;

  /**
   * Try to configure the connection's initial congestion window.
   * The operation is advisory - the connection may not support it, even if it's supported, it may
   * not do anything after the first few network round trips with the peer.
   * @param bandwidth_bits_per_sec The estimated bandwidth between the two endpoints of the
   * connection.
   * @param rtt The estimated round trip time between the two endpoints of the connection.
   */
  virtual void configureInitialCongestionWindow(uint64_t bandwidth_bits_per_sec,
                                                std::chrono::microseconds rtt) PURE;
};

using TransportSocketPtr = std::unique_ptr<TransportSocket>;

/**
 * Options for creating transport sockets.
 */
class TransportSocketOptions {
public:
  virtual ~TransportSocketOptions() = default;

  /**
   * @return the const optional server name to set in the transport socket, for example SNI for
   *         SSL, regardless of the upstream cluster configuration. Filters that influence
   *         upstream connection selection, such as tcp_proxy, should take this option into account
   *         and should pass it through to the connection pool to ensure the correct endpoints are
   *         selected and the upstream connection is set up accordingly.
   */
  virtual const absl::optional<std::string>& serverNameOverride() const PURE;

  /**
   * @return the optional overridden SAN names to verify, if the transport socket supports SAN
   *         verification.
   */
  virtual const std::vector<std::string>& verifySubjectAltNameListOverride() const PURE;

  /**
   * The application protocols to use when negotiating an upstream connection. When an application
   * protocol override is provided, it will *always* be used.
   * @return the optional overridden application protocols.
   */
  virtual const std::vector<std::string>& applicationProtocolListOverride() const PURE;

  /**
   * The application protocol(s) to use when negotiating an upstream connection and no other
   * application protocol has been configured. Both
   * TransportSocketOptions::applicationProtocolListOverride and application protocols configured
   * in the CommonTlsContext on the Cluster will take precedence.
   *
   * Note that this option is intended for intermediate code (e.g. the HTTP connection pools) to
   * specify a default ALPN when no specific values are specified elsewhere. As such, providing a
   * value here might not make sense prior to load balancing.
   * @return the optional fallback(s) for application protocols, for when they are not specified in
   *         the TLS configuration.
   */
  virtual const std::vector<std::string>& applicationProtocolFallback() const PURE;

  /**
   * @return optional PROXY protocol address information.
   */
  virtual absl::optional<Network::ProxyProtocolData> proxyProtocolOptions() const PURE;

  // Information for use by the http_11_proxy transport socket.
  struct Http11ProxyInfo {
    Http11ProxyInfo(std::string hostname, Network::Address::InstanceConstSharedPtr address)
        : hostname(hostname), proxy_address(address) {}
    // The hostname of the original request, to be used in CONNECT request if
    // the underlying transport is TLS.
    std::string hostname;
    // The address of the proxy, where connections should be routed to.
    Network::Address::InstanceConstSharedPtr proxy_address;
  };

  /**
   * @return any proxy information if sending to an intermediate proxy over HTTP/1.1.
   */
  virtual OptRef<const Http11ProxyInfo> http11ProxyInfo() const PURE;

  /**
   * @return filter state objects from the downstream request or connection
   * that are marked as shared with the upstream connection.
   */
  virtual const StreamInfo::FilterState::Objects& downstreamSharedFilterStateObjects() const PURE;
};

using TransportSocketOptionsConstSharedPtr = std::shared_ptr<const TransportSocketOptions>;

/**
 * A factory for creating transport sockets.
 **/
class TransportSocketFactoryBase {
public:
  virtual ~TransportSocketFactoryBase() = default;

  /**
   * @return bool whether the transport socket implements secure transport.
   */
  virtual bool implementsSecureTransport() const PURE;
};

/**
 * A factory for creating upstream transport sockets. It will be associated to clusters.
 */
class UpstreamTransportSocketFactory : public virtual TransportSocketFactoryBase {
public:
  ~UpstreamTransportSocketFactory() override = default;

  /**
   * @param options for creating the transport socket
   * @param host description for the destination upstream host
   * @return Network::TransportSocketPtr a transport socket to be passed to client connection.
   */
  virtual TransportSocketPtr
  createTransportSocket(TransportSocketOptionsConstSharedPtr options,
                        std::shared_ptr<const Upstream::HostDescription> host) const PURE;

  /**
   * Returns true if the transport socket created by this factory supports some form of ALPN
   * negotiation.
   */
  virtual bool supportsAlpn() const { return false; }

  /**
   * Returns the default SNI for transport sockets created by this factory.
   * This will return an empty string view if the transport sockets created are
   * not client-side TLS sockets.
   */
  virtual absl::string_view defaultServerNameIndication() const PURE;

  /**
   * @param key supplies a vector of bytes to which the option should append hash key data that will
   *        be used to separate connections based on the option. Any data already in the key vector
   *        must not be modified.
   * @param options supplies the transport socket options.
   */
  virtual void hashKey(std::vector<uint8_t>& key,
                       TransportSocketOptionsConstSharedPtr options) const PURE;

  /*
   * @return the pointer to the SSL context, or nullptr for non-TLS factories.
   */
  virtual Envoy::Ssl::ClientContextSharedPtr sslCtx() { return nullptr; }

  /*
   * @return the ClientContextConfig, or absl::nullopt for non-TLS factories.
   */
  virtual OptRef<const Ssl::ClientContextConfig> clientContextConfig() const { return {}; }

  /*
   * @return the QuicCryptoClientConfig or nullptr for non-QUIC factories.
   */
  virtual std::shared_ptr<quic::QuicCryptoClientConfig> getCryptoConfig() { return nullptr; }
};

/**
 * A factory for creating downstream transport sockets. It will be associated to listeners.
 */
class DownstreamTransportSocketFactory : public virtual TransportSocketFactoryBase {
public:
  ~DownstreamTransportSocketFactory() override = default;

  /**
   * @return Network::TransportSocketPtr a transport socket to be passed to server connection.
   */
  virtual TransportSocketPtr createDownstreamTransportSocket() const PURE;
};

using UpstreamTransportSocketFactoryPtr = std::unique_ptr<UpstreamTransportSocketFactory>;
using DownstreamTransportSocketFactoryPtr = std::unique_ptr<DownstreamTransportSocketFactory>;

} // namespace Network
} // namespace Envoy
