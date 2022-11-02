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

bool UpstreamHttp11ConnectSocket::isValidConnectResponse(Buffer::Instance& buffer) {
  SelfContainedParser parser;
  while (parser.parser().getStatus() == Http::Http1::ParserStatus::Ok &&
         !parser.headersComplete() && buffer.length() != 0) {
    auto slice = buffer.frontSlice();
    int parsed = parser.parser().execute(static_cast<const char*>(slice.mem_), slice.len_);
    buffer.drain(parsed);
  }
  return parser.parser().getStatus() != Http::Http1::ParserStatus::Error &&
         parser.headersComplete() && parser.parser().statusCode() == 200;
}

UpstreamHttp11ConnectSocket::UpstreamHttp11ConnectSocket(
    Network::TransportSocketPtr&& transport_socket,
    Network::TransportSocketOptionsConstSharedPtr options)
    : PassthroughSocket(std::move(transport_socket)), options_(options) {
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
    size_t index = peek_data.find("\r\n\r\n");
    if (index == absl::string_view::npos) {
      if (result.return_value_ == MAX_RESPONSE_HEADER_SIZE) {
        ENVOY_CONN_LOG(trace, "failed to receive CONNECT headers within {} bytes",
                       callbacks_->connection(), MAX_RESPONSE_HEADER_SIZE);
        return {Network::PostIoAction::Close, 0, false};
      }
      return Network::IoResult{Network::PostIoAction::KeepOpen, 0, false};
    }
    result = callbacks_->ioHandle().read(buffer, index + 4);
    if (!result.ok() || result.return_value_ != index + 4) {
      ENVOY_CONN_LOG(trace, "failed to drain CONNECT header", callbacks_->connection());
      return {Network::PostIoAction::Close, 0, false};
    }
    // Make sure the response is a valid connect response and all the data is consumed.
    if (!isValidConnectResponse(buffer) || buffer.length() != 0) {
      ENVOY_CONN_LOG(trace, "Response does not appear to be a successful CONNECT upgrade",
                     callbacks_->connection());
      return {Network::PostIoAction::Close, 0, false};
    }
    ENVOY_CONN_LOG(trace, "Successfully stripped CONNECT header", callbacks_->connection());
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
    Network::UpstreamTransportSocketFactoryPtr transport_socket_factory)
    : PassthroughFactory(std::move(transport_socket_factory)) {}

Network::TransportSocketPtr UpstreamHttp11ConnectSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsConstSharedPtr options,
    std::shared_ptr<const Upstream::HostDescription> host) const {
  auto inner_socket = transport_socket_factory_->createTransportSocket(options, host);
  if (inner_socket == nullptr) {
    return nullptr;
  }
  return std::make_unique<UpstreamHttp11ConnectSocket>(std::move(inner_socket), options);
}

void UpstreamHttp11ConnectSocketFactory::hashKey(
    std::vector<uint8_t>& key, Network::TransportSocketOptionsConstSharedPtr options) const {
  PassthroughFactory::hashKey(key, options);
  if (options && options->http11ProxyInfo().has_value()) {
    pushScalarToByteVector(
        StringUtil::CaseInsensitiveHash()(options->http11ProxyInfo()->proxy_address->asString()),
        key);
    pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(options->http11ProxyInfo()->hostname),
                           key);
  }
}

} // namespace Http11Connect
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
