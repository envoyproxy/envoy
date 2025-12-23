#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/rc_connection_wrapper.h"

#include "envoy/network/address.h"
#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_socket_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator_extension.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// RCConnectionWrapper constructor implementation
RCConnectionWrapper::RCConnectionWrapper(ReverseConnectionIOHandle& parent,
                                         Network::ClientConnectionPtr connection,
                                         Upstream::HostDescriptionConstSharedPtr host,
                                         const std::string& cluster_name)
    : parent_(parent), connection_(std::move(connection)), host_(std::move(host)),
      cluster_name_(cluster_name) {
  ENVOY_LOG(debug, "RCConnectionWrapper: Using HTTP handshake for reverse connections");
}

// RCConnectionWrapper destructor implementation.
RCConnectionWrapper::~RCConnectionWrapper() {
  ENVOY_LOG(debug, "RCConnectionWrapper destructor called");
  if (!shutdown_called_) {
    this->shutdown();
  }
}

void RCConnectionWrapper::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose) {
    if (!connection_) {
      ENVOY_LOG(debug, "RCConnectionWrapper: connection is null, skipping event handling");
      return;
    }

    // Store connection info before it gets invalidated.
    const std::string connectionKey =
        connection_->connectionInfoProvider().localAddress()->asString();
    const uint64_t connectionId = connection_->id();

    ENVOY_LOG(debug, "RCConnectionWrapper: connection: {}, found connection {} remote closed",
              connectionId, connectionKey);

    // Don't call shutdown() here as it may cause cleanup during event processing
    // Instead, just notify parent of closure.
    parent_.onConnectionDone("Connection closed", this, true);
  }
}

// SimpleConnReadFilter::onData implementation.
Network::FilterStatus SimpleConnReadFilter::onData(Buffer::Instance& buffer, bool end_stream) {
  if (parent_ == nullptr) {
    return Network::FilterStatus::StopIteration;
  }

  // Cast parent_ back to RCConnectionWrapper.
  RCConnectionWrapper* wrapper = static_cast<RCConnectionWrapper*>(parent_);

  wrapper->dispatchHttp1(buffer);
  UNREFERENCED_PARAMETER(end_stream);
  return Network::FilterStatus::StopIteration;
}

std::string RCConnectionWrapper::connect(const std::string& src_tenant_id,
                                         const std::string& src_cluster_id,
                                         const std::string& src_node_id) {
  // Register connection callbacks.
  ENVOY_LOG(debug, "RCConnectionWrapper: connection: {}, adding connection callbacks",
            connection_->id());
  connection_->addConnectionCallbacks(*this);
  connection_->connect();

  // Use HTTP handshake.
  ENVOY_LOG(debug,
            "RCConnectionWrapper: connection: {}, sending reverse connection creation "
            "request through HTTP",
            connection_->id());

  // Create HTTP/1 codec to parse the response.
  Http::Http1Settings http1_settings = host_->cluster().httpProtocolOptions().http1Settings();
  http1_client_codec_ = std::make_unique<Http::Http1::ClientConnectionImpl>(
      *connection_, host_->cluster().http1CodecStats(), *this, http1_settings,
      host_->cluster().maxResponseHeadersKb(), host_->cluster().maxResponseHeadersCount());
  http1_parse_connection_ = http1_client_codec_.get();

  // Add a tiny read filter to feed bytes into the codec for response parsing.
  connection_->addReadFilter(Network::ReadFilterSharedPtr{new SimpleConnReadFilter(this)});

  // Build HTTP handshake headers with identifiers.
  absl::string_view tenant_id = src_tenant_id;
  absl::string_view cluster_id = src_cluster_id;
  absl::string_view node_id = src_node_id;
  std::string host_value;
  const auto& remote_address = connection_->connectionInfoProvider().remoteAddress();
  // This is used when reverse connections need to be established through a HTTP proxy.
  // The reverse connection listener connects to an internal cluster, to which an
  // internal listener listens. This internal listener has tunneling configuration
  // to tcp proxy the reverse connection requests over HTTP/1 CONNECT to the remote
  // proxy.
  if (remote_address->type() == Network::Address::Type::EnvoyInternal) {
    const auto& internal_address =
        std::dynamic_pointer_cast<const Network::Address::EnvoyInternalInstance>(remote_address);
    ENVOY_LOG(debug,
              "RCConnectionWrapper: connection: {}, remote address is internal "
              "listener {}, using endpoint ID in host header",
              connection_->id(), internal_address->envoyInternalAddress()->addressId());
    host_value = internal_address->envoyInternalAddress()->endpointId();
  } else {
    host_value = remote_address->asString();
    ENVOY_LOG(debug,
              "RCConnectionWrapper: connection: {}, remote address is external, "
              "using address as host header",
              connection_->id());
  }
  const Http::LowerCaseString& node_hdr =
      ::Envoy::Extensions::Bootstrap::ReverseConnection::reverseTunnelNodeIdHeader();
  const Http::LowerCaseString& cluster_hdr =
      ::Envoy::Extensions::Bootstrap::ReverseConnection::reverseTunnelClusterIdHeader();
  const Http::LowerCaseString& tenant_hdr =
      ::Envoy::Extensions::Bootstrap::ReverseConnection::reverseTunnelTenantIdHeader();

  auto headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>({
      {Http::Headers::get().Method, Http::Headers::get().MethodValues.Get},
      {Http::Headers::get().Path, "/reverse_connections/request"},
      {Http::Headers::get().Host, host_value},
  });
  headers->addCopy(node_hdr, std::string(node_id));
  headers->addCopy(cluster_hdr, std::string(cluster_id));
  headers->addCopy(tenant_hdr, std::string(tenant_id));
  headers->setContentLength(0);

  // Encode via HTTP/1 codec.
  Http::RequestEncoder& request_encoder = http1_client_codec_->newStream(*this);
  const Http::Status encode_status = request_encoder.encodeHeaders(*headers, true);
  if (!encode_status.ok()) {
    ENVOY_LOG(error, "RCConnectionWrapper: encodeHeaders failed: {}", encode_status.message());
    onHandshakeFailure(HandshakeFailureReason::encodeError());
  }

  return connection_->connectionInfoProvider().localAddress()->asString();
}

void RCConnectionWrapper::decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool) {
  const uint64_t status = Http::Utility::getResponseStatus(*headers);
  if (status == 200) {
    ENVOY_LOG(debug, "Received HTTP 200 OK response");
    onHandshakeSuccess();
  } else {
    ENVOY_LOG(error, "Received non-200 HTTP response: {}", status);
    onHandshakeFailure(HandshakeFailureReason::httpStatusError(absl::StrCat(status)));
  }
}

void RCConnectionWrapper::dispatchHttp1(Buffer::Instance& buffer) {
  if (http1_parse_connection_ != nullptr) {
    const Http::Status status = http1_parse_connection_->dispatch(buffer);
    if (!status.ok()) {
      ENVOY_LOG(debug, "RCConnectionWrapper: HTTP/1 codec dispatch error: {}", status.message());
    }
  }
}

ReverseTunnelInitiatorExtension* RCConnectionWrapper::getDownstreamExtension() const {
  return parent_.getDownstreamExtension();
}

void RCConnectionWrapper::onHandshakeSuccess() {
  std::string message = "reverse connection accepted";
  ENVOY_LOG(debug, "handshake succeeded: {}", message);

  // Track handshake success stats.
  auto* extension = getDownstreamExtension();
  if (extension) {
    extension->incrementHandshakeStats(cluster_name_, true, "");
  }

  parent_.onConnectionDone(message, this, false);
}

void RCConnectionWrapper::onHandshakeFailure(const HandshakeFailureReason& reason) {
  const std::string error_message = reason.getDetailedName();
  const std::string stats_failure_reason = reason.getNameForStats();

  ENVOY_LOG(trace, "handshake failed: {}", error_message);

  // Track handshake failure stats.
  auto* extension = getDownstreamExtension();
  if (extension) {
    extension->incrementHandshakeStats(cluster_name_, false, stats_failure_reason);
  }

  parent_.onConnectionDone(error_message, this, false);
}

void RCConnectionWrapper::shutdown() {
  if (shutdown_called_) {
    ENVOY_LOG(debug, "RCConnectionWrapper: Shutdown already called, skipping");
    return;
  }
  shutdown_called_ = true;

  if (!connection_) {
    ENVOY_LOG(error, "RCConnectionWrapper: Connection already null, nothing to shutdown");
    return;
  }

  // Get connection info for logging.
  uint64_t connection_id = connection_->id();
  Network::Connection::State state = connection_->state();
  ENVOY_LOG(debug, "RCConnectionWrapper: Shutting down connection ID: {}, state: {}", connection_id,
            static_cast<int>(state));

  // Remove connection callbacks first to prevent recursive calls during shutdown.
  if (state != Network::Connection::State::Closed) {
    connection_->removeConnectionCallbacks(*this);
    ENVOY_LOG(debug, "Connection callbacks removed");
  }

  // Close the connection if it's still open.
  state = connection_->state();
  if (state == Network::Connection::State::Open) {
    ENVOY_LOG(debug, "Closing open connection gracefully");
    connection_->close(Network::ConnectionCloseType::FlushWrite);
  } else if (state == Network::Connection::State::Closing) {
    ENVOY_LOG(debug, "Connection already closing");
  } else {
    ENVOY_LOG(debug, "Connection already closed");
  }

  // Clear the connection pointer after shutdown.
  connection_.reset();
  ENVOY_LOG(debug, "RCConnectionWrapper: Connection cleared after shutdown");
  ENVOY_LOG(debug, "RCConnectionWrapper: Shutdown completed");
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
