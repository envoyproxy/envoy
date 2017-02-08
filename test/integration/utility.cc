#include "utility.h"

#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"

#include "common/api/api_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

void BufferingStreamDecoder::decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) {
  ASSERT(!complete_);
  complete_ = end_stream;
  headers_ = std::move(headers);
  if (complete_) {
    onComplete();
  }
}

void BufferingStreamDecoder::decodeData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!complete_);
  complete_ = end_stream;
  body_.append(TestUtility::bufferToString(data));
  if (complete_) {
    onComplete();
  }
}

void BufferingStreamDecoder::decodeTrailers(Http::HeaderMapPtr&&) { NOT_IMPLEMENTED; }

void BufferingStreamDecoder::onComplete() {
  ASSERT(complete_);
  on_complete_cb_();
}

void BufferingStreamDecoder::onResetStream(Http::StreamResetReason) { ADD_FAILURE(); }

BufferingStreamDecoderPtr
IntegrationUtil::makeSingleRequest(uint32_t port, const std::string& method, const std::string& url,
                                   const std::string& body, Http::CodecClient::Type type,
                                   const std::string& host) {
  Api::Impl api(std::chrono::milliseconds(9000));
  Event::DispatcherPtr dispatcher(api.allocateDispatcher());
  std::shared_ptr<Upstream::MockClusterInfo> cluster{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostDescriptionPtr host_description{new Upstream::HostDescriptionImpl(
      cluster, Network::Utility::resolveUrl("tcp://127.0.0.1:80"), false, "")};
  Http::CodecClientProd client(type,
                               dispatcher->createClientConnection(Network::Utility::resolveUrl(
                                   fmt::format("tcp://127.0.0.1:{}", port))),
                               host_description);
  BufferingStreamDecoderPtr response(new BufferingStreamDecoder([&]() -> void { client.close(); }));
  Http::StreamEncoder& encoder = client.newStream(*response);
  encoder.getStream().addCallbacks(*response);

  Http::HeaderMapImpl headers;
  headers.insertMethod().value(method);
  headers.insertPath().value(url);
  headers.insertHost().value(host);
  headers.insertScheme().value(Http::Headers::get().SchemeValues.Http);
  encoder.encodeHeaders(headers, body.empty());
  if (!body.empty()) {
    Buffer::OwnedImpl body_buffer(body);
    encoder.encodeData(body_buffer, true);
  }

  dispatcher->run(Event::Dispatcher::RunType::Block);
  return response;
}

RawConnectionDriver::RawConnectionDriver(uint32_t port, Buffer::Instance& initial_data,
                                         ReadCallback data_callback) {
  api_.reset(new Api::Impl(std::chrono::milliseconds(10000)));
  dispatcher_ = api_->allocateDispatcher();
  client_ = dispatcher_->createClientConnection(
      Network::Utility::resolveUrl(fmt::format("tcp://127.0.0.1:{}", port)));
  client_->addReadFilter(Network::ReadFilterPtr{new ForwardingFilter(*this, data_callback)});
  client_->write(initial_data);
  client_->connect();
}

RawConnectionDriver::~RawConnectionDriver() {}

void RawConnectionDriver::run() { dispatcher_->run(Event::Dispatcher::RunType::Block); }

void RawConnectionDriver::close() { client_->close(Network::ConnectionCloseType::FlushWrite); }
