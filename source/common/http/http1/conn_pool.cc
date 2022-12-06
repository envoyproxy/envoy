#include "source/common/http/http1/conn_pool.h"

#include <cstdint>
#include <list>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/schedulable_cb.h"
#include "envoy/event/timer.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/upstream/upstream.h"

#include "source/common/http/codec_client.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Http {
namespace Http1 {

ActiveClient::StreamWrapper::StreamWrapper(ResponseDecoder& response_decoder, ActiveClient& parent)
    : RequestEncoderWrapper(&parent.codec_client_->newStream(*this)),
      ResponseDecoderWrapper(response_decoder), parent_(parent) {
  RequestEncoderWrapper::inner_encoder_->getStream().addCallbacks(*this);
}

ActiveClient::StreamWrapper::~StreamWrapper() {
  // Upstream connection might be closed right after response is complete. Setting delay=true
  // here to attach pending requests in next dispatcher loop to handle that case.
  // https://github.com/envoyproxy/envoy/issues/2715
  parent_.parent_.onStreamClosed(parent_, true);
}

void ActiveClient::StreamWrapper::onEncodeComplete() { encode_complete_ = true; }

void ActiveClient::StreamWrapper::decodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) {
  close_connection_ =
      HeaderUtility::shouldCloseConnection(parent_.codec_client_->protocol(), *headers);
  if (close_connection_) {
    parent_.parent().host()->cluster().trafficStats().upstream_cx_close_notify_.inc();
  }
  ResponseDecoderWrapper::decodeHeaders(std::move(headers), end_stream);
}

void ActiveClient::StreamWrapper::onDecodeComplete() {
  ASSERT(!decode_complete_);
  decode_complete_ = encode_complete_;
  ENVOY_CONN_LOG(debug, "response complete", *parent_.codec_client_);

  if (!parent_.stream_wrapper_->encode_complete_) {
    ENVOY_CONN_LOG(debug, "response before request complete", *parent_.codec_client_);
    parent_.codec_client_->close();
  } else if (parent_.stream_wrapper_->close_connection_ || parent_.codec_client_->remoteClosed()) {
    ENVOY_CONN_LOG(debug, "saw upstream close connection", *parent_.codec_client_);
    parent_.codec_client_->close();
  } else {
    auto* pool = &parent_.parent();
    pool->scheduleOnUpstreamReady();
    parent_.stream_wrapper_.reset();

    pool->checkForIdleAndCloseIdleConnsIfDraining();
  }
}

void ActiveClient::StreamWrapper::onResetStream(StreamResetReason, absl::string_view) {
  parent_.codec_client_->close();
}

ActiveClient::ActiveClient(HttpConnPoolImplBase& parent,
                           OptRef<Upstream::Host::CreateConnectionData> data)
    : Envoy::Http::ActiveClient(parent, parent.host()->cluster().maxRequestsPerConnection(),
                                /* effective_concurrent_stream_limit */ 1,
                                /* configured_concurrent_stream_limit */ 1, data) {
  parent.host()->cluster().trafficStats().upstream_cx_http1_total_.inc();
}

ActiveClient::~ActiveClient() { ASSERT(!stream_wrapper_.get()); }

bool ActiveClient::closingWithIncompleteStream() const {
  return (stream_wrapper_ != nullptr) && (!stream_wrapper_->decode_complete_);
}

RequestEncoder& ActiveClient::newStreamEncoder(ResponseDecoder& response_decoder) {
  ASSERT(!stream_wrapper_);
  stream_wrapper_ = std::make_unique<StreamWrapper>(response_decoder, *this);
  return *stream_wrapper_;
}

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state) {
  return std::make_unique<FixedHttpConnPoolImpl>(
      std::move(host), std::move(priority), dispatcher, options, transport_socket_options,
      random_generator, state,
      [](HttpConnPoolImplBase* pool) {
        return std::make_unique<ActiveClient>(*pool, absl::nullopt);
      },
      [](Upstream::Host::CreateConnectionData& data, HttpConnPoolImplBase* pool) {
        CodecClientPtr codec{new CodecClientProd(
            CodecType::HTTP1, std::move(data.connection_), data.host_description_,
            pool->dispatcher(), pool->randomGenerator(), pool->transportSocketOptions())};
        return codec;
      },
      std::vector<Protocol>{Protocol::Http11}, absl::nullopt, nullptr);
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
