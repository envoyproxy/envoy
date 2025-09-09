#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/rc_connection_wrapper.h"

#include "envoy/network/address.h"
#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_socket_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_handshake.pb.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"

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

// RCConnectionWrapper destructor implementation
RCConnectionWrapper::~RCConnectionWrapper() {
  ENVOY_LOG(debug, "RCConnectionWrapper destructor called");
  this->shutdown();
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

// SimpleConnReadFilter::onData implementation
Network::FilterStatus SimpleConnReadFilter::onData(Buffer::Instance& buffer, bool) {
  if (parent_ == nullptr) {
    return Network::FilterStatus::StopIteration;
  }

  // Cast parent_ back to RCConnectionWrapper
  RCConnectionWrapper* wrapper = static_cast<RCConnectionWrapper*>(parent_);

  const std::string data = buffer.toString();

  // Look for HTTP response status line first (supports both HTTP/1.1 and HTTP/2)
  if (data.find("HTTP/1.1 200 OK") != std::string::npos ||
      data.find("HTTP/2 200") != std::string::npos) {
    ENVOY_LOG(debug, "Received HTTP 200 OK response");

    // Find the end of headers (double CRLF)
    size_t headers_end = data.find("\r\n\r\n");
    if (headers_end != std::string::npos) {
      // Extract the response body (after headers)
      std::string response_body = data.substr(headers_end + 4);

      if (!response_body.empty()) {
        // Try to parse the protobuf response
        envoy::extensions::bootstrap::reverse_tunnel::ReverseConnHandshakeRet ret;
        if (ret.ParseFromString(response_body)) {
          ENVOY_LOG(debug, "Successfully parsed protobuf response: {}", ret.DebugString());

          // Check if the status is ACCEPTED
          if (ret.status() ==
              envoy::extensions::bootstrap::reverse_tunnel::ReverseConnHandshakeRet::ACCEPTED) {
            ENVOY_LOG(debug, "SimpleConnReadFilter: Reverse connection accepted by cloud side");
            wrapper->onHandshakeSuccess();
            return Network::FilterStatus::StopIteration;
          } else {
            ENVOY_LOG(error, "SimpleConnReadFilter: Reverse connection rejected: {}",
                      ret.status_message());
            wrapper->onHandshakeFailure(ret.status_message());
            return Network::FilterStatus::StopIteration;
          }
        } else {
          ENVOY_LOG(error, "Could not parse protobuf response - invalid response format");
          wrapper->onHandshakeFailure(
              "Invalid response format - expected ReverseConnHandshakeRet protobuf");
          return Network::FilterStatus::StopIteration;
        }
      } else {
        ENVOY_LOG(debug, "Response body is empty, waiting for more data");
        return Network::FilterStatus::Continue;
      }
    } else {
      ENVOY_LOG(debug, "HTTP headers not complete yet, waiting for more data");
      return Network::FilterStatus::Continue;
    }
  } else if (data.find("HTTP/1.1 ") != std::string::npos ||
             data.find("HTTP/2 ") != std::string::npos) {
    // Found HTTP response but not 200 OK - this is an error
    ENVOY_LOG(error, "Received non-200 HTTP response: {}", data.substr(0, 100));
    wrapper->onHandshakeFailure("HTTP handshake failed with non-200 response");
    return Network::FilterStatus::StopIteration;
  } else {
    ENVOY_LOG(debug, "Waiting for HTTP response, received {} bytes", data.length());
    return Network::FilterStatus::Continue;
  }
}

std::string RCConnectionWrapper::connect(const std::string& src_tenant_id,
                                         const std::string& src_cluster_id,
                                         const std::string& src_node_id) {
  // Register connection callbacks.
  ENVOY_LOG(debug, "RCConnectionWrapper: connection: {}, adding connection callbacks",
            connection_->id());
  connection_->addConnectionCallbacks(*this);
  connection_->connect();

  // Use HTTP handshake
  ENVOY_LOG(debug,
            "RCConnectionWrapper: connection: {}, sending reverse connection creation "
            "request through HTTP",
            connection_->id());

  // Add read filter to handle HTTP response
  connection_->addReadFilter(Network::ReadFilterSharedPtr{new SimpleConnReadFilter(this)});

  // Use HTTP handshake logic
  envoy::extensions::bootstrap::reverse_tunnel::ReverseConnHandshakeArg arg;
  arg.set_tenant_uuid(src_tenant_id);
  arg.set_cluster_uuid(src_cluster_id);
  arg.set_node_uuid(src_node_id);
  ENVOY_LOG(debug,
            "RCConnectionWrapper: Creating protobuf with tenant='{}', cluster='{}', node='{}'",
            src_tenant_id, src_cluster_id, src_node_id);
  std::string body = arg.SerializeAsString(); // NOLINT(protobuf-use-MessageUtil-hash)
  ENVOY_LOG(debug, "RCConnectionWrapper: Serialized protobuf body length: {}, debug: '{}'",
            body.length(), arg.DebugString());
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
  // Build HTTP request with protobuf body.
  Buffer::OwnedImpl reverse_connection_request(
      fmt::format("POST /reverse_connections/request HTTP/1.1\r\n"
                  "Host: {}\r\n"
                  "Accept: */*\r\n"
                  "Content-length: {}\r\n"
                  "\r\n{}",
                  host_value, body.length(), body));
  ENVOY_LOG(debug, "RCConnectionWrapper: connection: {}, writing request to connection: {}",
            connection_->id(), reverse_connection_request.toString());
  // Send reverse connection request over TCP connection.
  connection_->write(reverse_connection_request, false);

  return connection_->connectionInfoProvider().localAddress()->asString();
}

void RCConnectionWrapper::onHandshakeSuccess() {
  std::string message = "reverse connection accepted";
  ENVOY_LOG(debug, "handshake succeeded: {}", message);
  parent_.onConnectionDone(message, this, false);
}

void RCConnectionWrapper::onHandshakeFailure(const std::string& message) {
  ENVOY_LOG(debug, "handshake failed: {}", message);
  parent_.onConnectionDone(message, this, false);
}

void RCConnectionWrapper::shutdown() {
  if (!connection_) {
    ENVOY_LOG(error, "RCConnectionWrapper: Connection already null, nothing to shutdown");
    return;
  }

  ENVOY_LOG(debug, "RCConnectionWrapper: Shutting down connection ID: {}, state: {}",
            connection_->id(), static_cast<int>(connection_->state()));

  // Remove connection callbacks first to prevent recursive calls during shutdown.
  auto state = connection_->state();
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

  // Clear the connection pointer to prevent further access.
  connection_.reset();
  ENVOY_LOG(debug, "RCConnectionWrapper: Shutdown completed");
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
