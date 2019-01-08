#include "common/aws/metadata_fetcher_impl.h"

#include "envoy/common/exception.h"
#include "envoy/network/transport_socket.h"

#include "common/http/headers.h"
#include "common/http/http1/codec_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Aws {
namespace Auth {

const size_t MetadataFetcherImpl::MAX_RETRIES = 4;
const std::chrono::milliseconds MetadataFetcherImpl::NO_DELAY{0};
const std::chrono::milliseconds MetadataFetcherImpl::RETRY_DELAY{1000};
const std::chrono::milliseconds MetadataFetcherImpl::TIMEOUT{5000};

absl::optional<std::string>
MetadataFetcherImpl::getMetadata(Event::Dispatcher& dispatcher, const std::string& host,
                                 const std::string& path,
                                 const absl::optional<std::string>& auth_token) const {
  // default to port 80
  auto host_with_port = host;
  if (host.find(':') == std::string::npos) {
    host_with_port = host + ":80";
  }
  Http::HeaderMapImpl headers;
  headers.insertMethod().value().setReference(Http::Headers::get().MethodValues.Get);
  headers.insertHost().value().setReference(host_with_port);
  headers.insertPath().value().setReference(path);
  if (auth_token) {
    headers.insertAuthorization().value().setReference(auth_token.value());
  }
  auto delay = NO_DELAY;
  for (size_t retries = 0; retries < MAX_RETRIES; retries++) {
    ENVOY_LOG(debug, "Connecting to http://{}{} to retrieve metadata. Try {}/{}", host_with_port,
              path, retries + 1, MAX_RETRIES);
    auto decoder = std::unique_ptr<MetadataFetcherImpl::StringBufferDecoder>(decoder_factory_());
    std::unique_ptr<MetadataSession> session;
    const auto timer = dispatcher.createTimer([&]() {
      auto socket = std::make_unique<Network::RawBufferSocket>();
      auto options = std::make_shared<Network::Socket::Options>();
      const auto address =
          Network::Utility::resolveUrl(Network::Utility::TCP_SCHEME + host_with_port);
      session = std::unique_ptr<MetadataSession>(session_factory_(
          dispatcher.createClientConnection(address, nullptr, std::move(socket), options),
          dispatcher, *decoder, *decoder, headers, codec_factory_));
      decoder->setCompleteCallback([&session, &dispatcher]() {
        session->close();
        dispatcher.exit();
      });
    });
    timer->enableTimer(delay);
    dispatcher.run(Event::Dispatcher::RunType::Block);
    const auto& body = decoder->body();
    if (!body.empty()) {
      ENVOY_LOG(debug, "Found metadata at {}{}", host_with_port, path);
      return absl::optional<std::string>(body);
    }
    delay = RETRY_DELAY;
  }
  ENVOY_LOG(error, "Could not find metadata at {}{}", host_with_port, path);
  return absl::optional<std::string>();
}

Http::ClientConnection* MetadataFetcherImpl::createCodec(Network::Connection& connection,
                                                         Http::ConnectionCallbacks& callbacks) {
  return new Http::Http1::ClientConnectionImpl(connection, callbacks);
}

MetadataFetcherImpl::MetadataSession::MetadataSession(Network::ClientConnectionPtr&& connection,
                                                      Event::Dispatcher& dispatcher,
                                                      Http::StreamDecoder& decoder,
                                                      Http::StreamCallbacks& callbacks,
                                                      const Http::HeaderMap& headers,
                                                      HttpCodecFactory codec_factory)
    : connection_(std::move(connection)) {
  codec_ = Http::ClientConnectionPtr{codec_factory(*connection_, *this)};
  connection_->addReadFilter(Network::ReadFilterSharedPtr{new CodecReadFilter(*this)});
  connection_->addConnectionCallbacks(*this);
  connection_->connect();
  connection_->noDelay(true);
  encoder_ = &codec_->newStream(decoder);
  encoder_->getStream().addCallbacks(callbacks);
  encoder_->encodeHeaders(headers, true);
  timeout_timer_ = dispatcher.createTimer([this]() { close(); });
  timeout_timer_->enableTimer(TIMEOUT);
}

void MetadataFetcherImpl::MetadataSession::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected) {
    connected_ = true;
  }
  if (event == Network::ConnectionEvent::RemoteClose) {
    Buffer::OwnedImpl empty;
    onData(empty);
  }
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    encoder_->getStream().resetStream(connected_ ? Http::StreamResetReason::ConnectionTermination
                                                 : Http::StreamResetReason::ConnectionFailure);
  }
}

void MetadataFetcherImpl::MetadataSession::onData(Buffer::Instance& data) {
  try {
    codec_->dispatch(data);
  } catch (EnvoyException& e) {
    close();
  }
}

void MetadataFetcherImpl::StringBufferDecoder::decodeHeaders(Envoy::Http::HeaderMapPtr&&,
                                                             bool end_stream) {
  if (end_stream) {
    complete();
  }
}

void MetadataFetcherImpl::StringBufferDecoder::decodeData(Envoy::Buffer::Instance& data,
                                                          bool end_stream) {
  body_.append(data.toString());
  if (end_stream) {
    complete();
  }
}

} // namespace Auth
} // namespace Aws
} // namespace Envoy