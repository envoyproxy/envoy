#include "source/extensions/transport_sockets/http_11_proxy/connect.h"

#include <sstream>

#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Http11Connect {

bool UpstreamHttp11ConnectSocket::isValidConnectResponse(absl::string_view response_payload,
                                                         bool& headers_complete,
                                                         size_t& bytes_processed) {
  SelfContainedParser parser;

  bytes_processed = parser.parser().execute(response_payload.data(), response_payload.length());
  headers_complete = parser.headersComplete();

  return parser.parser().getStatus() != Http::Http1::ParserStatus::Error &&
         parser.headersComplete() && parser.parser().statusCode() == Http::Code::OK;
}

UpstreamHttp11ConnectSocket::UpstreamHttp11ConnectSocket(
    Network::TransportSocketPtr&& transport_socket,
    Network::TransportSocketOptionsConstSharedPtr options,
    std::shared_ptr<const Upstream::HostDescription> host, bool legacy_behavior)
    : PassthroughSocket(std::move(transport_socket)), options_(options),
      legacy_behavior_(legacy_behavior) {
  if (legacy_behavior_) {
    legacyConstructor();
    return;
  }

  header_buffer_.add(
      absl::StrCat("CONNECT ", host->address()->asStringView(), " HTTP/1.1\r\n\r\n"));
  need_to_strip_connect_response_ = true;
}

void UpstreamHttp11ConnectSocket::legacyConstructor() {
  if (options_ && options_->http11ProxyInfo() && transport_socket_->ssl()) {
    header_buffer_.add(
        absl::StrCat("CONNECT ", options_->http11ProxyInfo()->hostname, ":443 HTTP/1.1\r\n\r\n"));
    need_to_strip_connect_response_ = true;
  }
}

void UpstreamHttp11ConnectSocket::setTransportSocketCallbacks(
    Network::TransportSocketCallbacks& callbacks) {
  transport_socket_->setTransportSocketCallbacks(callbacks);
  callbacks_ = &callbacks;
}

Network::IoResult UpstreamHttp11ConnectSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  if (header_buffer_.length() > 0) {
    return writeHeader();
  }
  if (!need_to_strip_connect_response_) {
    // Don't pass events up until the connect response is read because TLS reads
    // kick off writes which don't pass through the transport socket.
    return transport_socket_->doWrite(buffer, end_stream);
  }
  return Network::IoResult{Network::PostIoAction::KeepOpen, 0, false};
}

Network::IoResult UpstreamHttp11ConnectSocket::doRead(Buffer::Instance& buffer) {
  if (need_to_strip_connect_response_) {
    // Limit the CONNECT response headers to an arbitrary 2000 bytes.
    constexpr uint32_t MAX_RESPONSE_HEADER_SIZE = 2000;
    char peek_buf[MAX_RESPONSE_HEADER_SIZE];
    Api::IoCallUint64Result result =
        callbacks_->ioHandle().recv(peek_buf, MAX_RESPONSE_HEADER_SIZE, MSG_PEEK);
    if (!result.ok() && result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
      return {Network::PostIoAction::Close, 0, false};
    }
    absl::string_view peek_data(peek_buf, result.return_value_);
    size_t bytes_processed = 0;
    bool headers_complete = false;
    bool is_valid_connect_response =
        isValidConnectResponse(peek_data, headers_complete, bytes_processed);

    if (!headers_complete) {
      if (peek_data.size() == MAX_RESPONSE_HEADER_SIZE) {
        ENVOY_CONN_LOG(trace, "failed to receive CONNECT headers within {} bytes",
                       callbacks_->connection(), MAX_RESPONSE_HEADER_SIZE);
        return {Network::PostIoAction::Close, 0, false};
      }
      ENVOY_CONN_LOG(trace, "Incomplete CONNECT header: {} bytes received",
                     callbacks_->connection(), peek_data.size());
      return Network::IoResult{Network::PostIoAction::KeepOpen, 0, false};
    }
    if (!is_valid_connect_response) {
      ENVOY_CONN_LOG(trace, "Response does not appear to be a successful CONNECT upgrade",
                     callbacks_->connection());
      return {Network::PostIoAction::Close, 0, false};
    }

    result = callbacks_->ioHandle().read(buffer, bytes_processed);
    if (!result.ok() || result.return_value_ != bytes_processed) {
      ENVOY_CONN_LOG(trace, "failed to drain CONNECT header", callbacks_->connection());
      return {Network::PostIoAction::Close, 0, false};
    }
    buffer.drain(bytes_processed);

    ENVOY_CONN_LOG(trace, "Successfully stripped {} bytes of CONNECT header",
                   callbacks_->connection(), bytes_processed);
    need_to_strip_connect_response_ = false;
  }
  return transport_socket_->doRead(buffer);
}

Network::IoResult UpstreamHttp11ConnectSocket::writeHeader() {
  Network::PostIoAction action = Network::PostIoAction::KeepOpen;
  uint64_t bytes_written = 0;
  do {
    if (header_buffer_.length() == 0) {
      break;
    }

    Api::IoCallUint64Result result = callbacks_->ioHandle().write(header_buffer_);

    if (!result.ok()) {
      ENVOY_CONN_LOG(trace, "Failed writing CONNECT header. write error: {}",
                     callbacks_->connection(), result.err_->getErrorDetails());
      if (result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
        action = Network::PostIoAction::Close;
      }
      break;
    }
    ENVOY_CONN_LOG(trace, "Writing CONNECT header. write returned: {}", callbacks_->connection(),
                   result.return_value_);
    bytes_written += result.return_value_;
  } while (true);

  return {action, bytes_written, false};
}

UpstreamHttp11ConnectSocketFactory::UpstreamHttp11ConnectSocketFactory(
    Network::UpstreamTransportSocketFactoryPtr transport_socket_factory,
    absl::optional<std::string> proto_proxy_address)
    : PassthroughFactory(std::move(transport_socket_factory)),
      proto_proxy_address_(proto_proxy_address) {}

Network::TransportSocketPtr UpstreamHttp11ConnectSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsConstSharedPtr options,
    std::shared_ptr<const Upstream::HostDescription> host) const {

  auto inner_socket = transport_socket_factory_->createTransportSocket(options, host);
  if (inner_socket == nullptr) {
    return nullptr;
  }

  return std::make_unique<UpstreamHttp11ConnectSocket>(std::move(inner_socket), options, host,
                                                       !proto_proxy_address_.has_value());
}

void UpstreamHttp11ConnectSocketFactory::hashKey(
    std::vector<uint8_t>& key, Network::TransportSocketOptionsConstSharedPtr options) const {
  PassthroughFactory::hashKey(key, options);

  if (proto_proxy_address_.has_value()) {
    pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(proto_proxy_address_.value()), key);
  }
}

} // namespace Http11Connect
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
