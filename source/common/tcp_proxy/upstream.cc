#include "common/tcp_proxy/upstream.h"

#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Envoy {
namespace TcpProxy {

TcpUpstream::TcpUpstream(Tcp::ConnectionPool::ConnectionDataPtr&& data,
                         Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks)
    : upstream_conn_data_(std::move(data)) {
  Network::ClientConnection& connection = upstream_conn_data_->connection();
  connection.enableHalfClose(true);
  upstream_conn_data_->addUpstreamCallbacks(upstream_callbacks);
}

bool TcpUpstream::readDisable(bool disable) {
  if (upstream_conn_data_ == nullptr ||
      upstream_conn_data_->connection().state() != Network::Connection::State::Open) {
    // Because we flush write downstream, we can have a case where upstream has already disconnected
    // and we are waiting to flush. If we had a watermark event during this time we should no
    // longer touch the upstream connection.
    return false;
  }

  upstream_conn_data_->connection().readDisable(disable);
  return true;
}

void TcpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  upstream_conn_data_->connection().write(data, end_stream);
}

void TcpUpstream::addBytesSentCallback(Network::Connection::BytesSentCb cb) {
  upstream_conn_data_->connection().addBytesSentCallback(cb);
}

Tcp::ConnectionPool::ConnectionData*
TcpUpstream::onDownstreamEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose) {
    // The close call may result in this object being deleted. Latch the
    // connection locally so it can be returned for potential draining.
    auto* conn_data = upstream_conn_data_.release();
    conn_data->connection().close(Network::ConnectionCloseType::FlushWrite);
    return conn_data;
  } else if (event == Network::ConnectionEvent::LocalClose) {
    upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
  }
  return nullptr;
}

HttpUpstream::HttpUpstream(Tcp::ConnectionPool::UpstreamCallbacks& callbacks,
                           const std::string& hostname)
    : upstream_callbacks_(callbacks), response_decoder_(*this), hostname_(hostname) {}

HttpUpstream::~HttpUpstream() { resetEncoder(Network::ConnectionEvent::LocalClose); }

bool HttpUpstream::isValidBytestreamResponse(const Http::ResponseHeaderMap& headers) {
  if (Http::Utility::getResponseStatus(headers) != 200) {
    return false;
  }
  return true;
}

bool HttpUpstream::readDisable(bool disable) {
  if (!request_encoder_) {
    return false;
  }
  request_encoder_->getStream().readDisable(disable);
  return true;
}

void HttpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!request_encoder_) {
    return;
  }
  request_encoder_->encodeData(data, end_stream);
  if (end_stream) {
    doneWriting();
  }
}

void HttpUpstream::addBytesSentCallback(Network::Connection::BytesSentCb) {
  // The HTTP tunneling mode does not tickle the idle timeout when bytes are
  // sent to the kernel.
  // This can be implemented if any user cares about the difference in time
  // between it being sent to the HTTP/2 stack and out to the kernel.
}

Tcp::ConnectionPool::ConnectionData*
HttpUpstream::onDownstreamEvent(Network::ConnectionEvent event) {
  if (event != Network::ConnectionEvent::Connected) {
    resetEncoder(Network::ConnectionEvent::LocalClose, false);
  }
  return nullptr;
}

void HttpUpstream::onResetStream(Http::StreamResetReason, absl::string_view) {
  read_half_closed_ = true;
  write_half_closed_ = true;
  resetEncoder(Network::ConnectionEvent::LocalClose);
}

void HttpUpstream::onAboveWriteBufferHighWatermark() {
  upstream_callbacks_.onAboveWriteBufferHighWatermark();
}

void HttpUpstream::onBelowWriteBufferLowWatermark() {
  upstream_callbacks_.onBelowWriteBufferLowWatermark();
}

void HttpUpstream::setRequestEncoder(Http::RequestEncoder& request_encoder, bool is_ssl) {
  request_encoder_ = &request_encoder;
  request_encoder_->getStream().addCallbacks(*this);
  const std::string& scheme =
      is_ssl ? Http::Headers::get().SchemeValues.Https : Http::Headers::get().SchemeValues.Http;
  auto headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
      {{Http::Headers::get().Method, "CONNECT"},
       {Http::Headers::get().Protocol, Http::Headers::get().ProtocolValues.Bytestream},
       {Http::Headers::get().Scheme, scheme},
       {Http::Headers::get().Path, "/"},
       {Http::Headers::get().Host, hostname_}});
  request_encoder_->encodeHeaders(*headers, false);
}

void HttpUpstream::resetEncoder(Network::ConnectionEvent event, bool inform_downstream) {
  if (!request_encoder_) {
    return;
  }
  request_encoder_->getStream().removeCallbacks(*this);
  if (!write_half_closed_ || !read_half_closed_) {
    request_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);
  }
  request_encoder_ = nullptr;
  if (inform_downstream) {
    upstream_callbacks_.onEvent(event);
  }
}

void HttpUpstream::doneReading() {
  read_half_closed_ = true;
  if (write_half_closed_) {
    resetEncoder(Network::ConnectionEvent::LocalClose);
  }
}

void HttpUpstream::doneWriting() {
  write_half_closed_ = true;
  if (read_half_closed_) {
    resetEncoder(Network::ConnectionEvent::LocalClose);
  }
}

TcpConnPool::TcpConnPool(const std::string& cluster_name, Upstream::ClusterManager& cluster_manager,
                         Upstream::LoadBalancerContext* context,
                         Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks)
    : upstream_callbacks_(upstream_callbacks) {
  conn_pool_ = cluster_manager.tcpConnPoolForCluster(cluster_name,
                                                     Upstream::ResourcePriority::Default, context);
}

TcpConnPool::~TcpConnPool() {
  if (upstream_handle_ != nullptr) {
    upstream_handle_->cancel(ConnectionPool::CancelPolicy::CloseExcess);
  }
}

bool TcpConnPool::valid() const { return conn_pool_ != nullptr; }

void TcpConnPool::newStream(GenericConnectionPoolCallbacks* callbacks) {
  callbacks_ = callbacks;
  // Given this function is reentrant, make sure we only reset the upstream_handle_ if given a
  // valid connection handle. If newConnection fails inline it may result in attempting to
  // select a new host, and a recursive call to initializeUpstreamConnection. In this case the
  // first call to newConnection will return null and the inner call will persist.
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_->newConnection(*this);
  if (handle) {
    ASSERT(upstream_handle_ == nullptr);
    upstream_handle_ = handle;
  }
}

void TcpConnPool::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  callbacks_->onGenericPoolFailure(reason, host);
}

void TcpConnPool::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                              Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  Tcp::ConnectionPool::ConnectionData* latched_data = conn_data.get();
  Network::Connection& connection = conn_data->connection();

  auto upstream = std::make_unique<TcpUpstream>(std::move(conn_data), upstream_callbacks_);
  callbacks_->onGenericPoolReady(&connection.streamInfo(), std::move(upstream), host,
                                 latched_data->connection().localAddress(),
                                 latched_data->connection().streamInfo().downstreamSslConnection());
}

HttpConnPool::HttpConnPool(const std::string& cluster_name,
                           Upstream::ClusterManager& cluster_manager,
                           Upstream::LoadBalancerContext* context, std::string hostname,
                           Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks)
    : hostname_(hostname), upstream_callbacks_(upstream_callbacks) {
  conn_pool_ = cluster_manager.httpConnPoolForCluster(
      cluster_name, Upstream::ResourcePriority::Default, absl::nullopt, context);
}

HttpConnPool::~HttpConnPool() {
  if (upstream_handle_ != nullptr) {
    // Because HTTP connections are generally shorter lived and have a higher probability of use
    // before going idle, they are closed with Default rather than CloseExcess.
    upstream_handle_->cancel(ConnectionPool::CancelPolicy::Default);
  }
}

bool HttpConnPool::valid() const { return conn_pool_ != nullptr; }

void HttpConnPool::newStream(GenericConnectionPoolCallbacks* callbacks) {
  callbacks_ = callbacks;
  upstream_ = std::make_unique<HttpUpstream>(upstream_callbacks_, hostname_);
  Tcp::ConnectionPool::Cancellable* handle =
      conn_pool_->newStream(upstream_->responseDecoder(), *this);
  if (handle != nullptr) {
    upstream_handle_ = handle;
  }
}

void HttpConnPool::onPoolFailure(ConnectionPool::PoolFailureReason reason, absl::string_view,
                                 Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  callbacks_->onGenericPoolFailure(reason, host);
}

void HttpConnPool::onPoolReady(Http::RequestEncoder& request_encoder,
                               Upstream::HostDescriptionConstSharedPtr host,
                               const StreamInfo::StreamInfo& info) {
  upstream_handle_ = nullptr;
  Http::RequestEncoder* latched_encoder = &request_encoder;
  upstream_->setRequestEncoder(request_encoder,
                               host->transportSocketFactory().implementsSecureTransport());
  callbacks_->onGenericPoolReady(nullptr, std::move(upstream_), host,
                                 latched_encoder->getStream().connectionLocalAddress(),
                                 info.downstreamSslConnection());
}

} // namespace TcpProxy
} // namespace Envoy
