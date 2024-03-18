#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_evaluator.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/upstream.h"

#include "source/common/router/router.h"

namespace Envoy {

namespace Upstream {
class LoadBalancerContext;
class ThreadLocalCluster;
} // namespace Upstream

namespace TcpProxy {

class GenericConnectionPoolCallbacks;
class GenericUpstream;

/**
 * A configuration for an individual tunneling TCP over HTTP protocols.
 */
class TunnelingConfigHelper {
public:
  virtual ~TunnelingConfigHelper() = default;

  // The host name of the tunneling upstream HTTP request.
  // This function evaluates command operators if specified. Otherwise it returns host name as is.
  virtual std::string host(const StreamInfo::StreamInfo& stream_info) const PURE;

  // The method of the upstream HTTP request. True if using POST method, CONNECT otherwise.
  virtual bool usePost() const PURE;

  // The path used for POST method.
  virtual const std::string& postPath() const PURE;

  // The evaluator to add additional HTTP request headers to the upstream request.
  virtual Envoy::Http::HeaderEvaluator& headerEvaluator() const PURE;

  // Save HTTP response headers to the downstream filter state.
  virtual void
  propagateResponseHeaders(Http::ResponseHeaderMapPtr&& headers,
                           const StreamInfo::FilterStateSharedPtr& filter_state) const PURE;

  // Save HTTP response trailers to the downstream filter state.
  virtual void
  propagateResponseTrailers(Http::ResponseTrailerMapPtr&& trailers,
                            const StreamInfo::FilterStateSharedPtr& filter_state) const PURE;
  virtual const Envoy::Router::FilterConfig& routerFilterConfig() const PURE;
  virtual Server::Configuration::ServerFactoryContext& serverFactoryContext() const PURE;
};

using TunnelingConfigHelperOptConstRef = OptRef<const TunnelingConfigHelper>;

// An API for wrapping either a TCP or an HTTP connection pool.
class GenericConnPool : public Event::DeferredDeletable,
                        public Logger::Loggable<Logger::Id::router> {
public:
  ~GenericConnPool() override = default;

  /**
   * Called to create a TCP connection or HTTP stream for "CONNECT" streams.
   *
   * The implementation is then responsible for calling either onGenericPoolReady or
   * onGenericPoolFailure on the supplied GenericConnectionPoolCallbacks.
   *
   * @param callbacks callbacks to communicate stream failure or creation on.
   */
  virtual void newStream(GenericConnectionPoolCallbacks& callbacks) PURE;
};

// An API for the UpstreamRequest to get callbacks from either an HTTP or TCP
// connection pool.
class GenericConnectionPoolCallbacks {
public:
  virtual ~GenericConnectionPoolCallbacks() = default;

  /**
   * Called when GenericConnPool::newStream has established a new stream.
   *
   * @param info supplies the stream info object associated with the upstream connection.
   * @param upstream supplies the generic upstream for the stream.
   * @param host supplies the description of the host that will carry the request.
   * @param address_provider supplies the address provider of the upstream connection.
   * @param ssl_info supplies the ssl information of the upstream connection.
   */
  virtual void onGenericPoolReady(StreamInfo::StreamInfo* info,
                                  std::unique_ptr<GenericUpstream>&& upstream,
                                  Upstream::HostDescriptionConstSharedPtr& host,
                                  const Network::ConnectionInfoProvider& address_provider,
                                  Ssl::ConnectionInfoConstSharedPtr ssl_info) PURE;

  /**
   * Called to indicate a failure for GenericConnPool::newStream to establish a stream.
   *
   * @param reason supplies the failure reason.
   * @param failure_reason failure reason string (Note: it is expected that the caller will provide
   * matching `reason` and `failure_reason`).
   * @param host supplies the description of the host that caused the failure. This may be nullptr
   *             if no host was involved in the failure (for example overflow).
   */
  virtual void onGenericPoolFailure(ConnectionPool::PoolFailureReason reason,
                                    absl::string_view failure_reason,
                                    Upstream::HostDescriptionConstSharedPtr host) PURE;
};

// Interface for a generic Upstream, which can communicate with a TCP or HTTP
// upstream.
class GenericUpstream : public Event::DeferredDeletable {
public:
  ~GenericUpstream() override = default;

  /**
   * Enable/disable further data from this stream.
   *
   * @param disable true if the stream should be read disabled, false otherwise.
   * @return returns true if the disable is performed, false otherwise
   *         (e.g. if the connection is closed)
   */
  virtual bool readDisable(bool disable) PURE;

  /**
   * Encodes data upstream.
   * @param data supplies the data to encode. The data may be moved by the encoder.
   * @param end_stream supplies whether this is the last data to encode.
   */
  virtual void encodeData(Buffer::Instance& data, bool end_stream) PURE;

  /**
   * Adds a callback to be called when the data is sent to the kernel.
   * @param cb supplies the callback to be called
   */
  virtual void addBytesSentCallback(Network::Connection::BytesSentCb cb) PURE;

  /**
   * Called when an event is received on the downstream connection
   * @param event supplies the event which occurred.
   * @return the underlying ConnectionData if the event is not "Connected" and draining
             is supported for this upstream.
   */
  virtual Tcp::ConnectionPool::ConnectionData*
  onDownstreamEvent(Network::ConnectionEvent event) PURE;

  /* Called to convert underlying transport socket from non-secure mode
   * to secure mode. Implemented only by start_tls transport socket.
   */
  virtual bool startUpstreamSecureTransport() PURE;

  /**
   * Called when upstream starttls socket is converted to tls and upstream ssl info
   * needs to be set in the connection's stream_info.
   * @return the const SSL connection data of upstream.
   */
  virtual Ssl::ConnectionInfoConstSharedPtr getUpstreamConnectionSslInfo() PURE;
};

using GenericConnPoolPtr = std::unique_ptr<GenericConnPool>;

/*
 * A factory for creating generic connection pools.
 */
class GenericConnPoolFactory : public Envoy::Config::TypedFactory {
public:
  ~GenericConnPoolFactory() override = default;

  /*
   * @param thread_local_cluster the thread local cluster to use for conn pool creation.
   * @param config the tunneling config, if doing connect tunneling.
   * @param context the load balancing context for this connection.
   * @param upstream_callbacks the callbacks to provide to the connection if successfully created.
   * @param downstream_info is the downstream connection stream info.
   * @return may be null if there is no cluster with the given name.
   */
  virtual GenericConnPoolPtr
  createGenericConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
                        TunnelingConfigHelperOptConstRef config,
                        Upstream::LoadBalancerContext* context,
                        Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks,
                        Http::StreamDecoderFilterCallbacks& stream_decoder_callbacks,
                        StreamInfo::StreamInfo& downstream_info) const PURE;
};

using GenericConnPoolFactoryPtr = std::unique_ptr<GenericConnPoolFactory>;

} // namespace TcpProxy
} // namespace Envoy
