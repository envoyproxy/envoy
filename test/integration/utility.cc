#include "utility.h"

#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"

#include "common/api/api_impl.h"
#include "common/common/assert.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/stats/stats_impl.h"

#include "test/test_common/utility.h"

void BufferingStreamDecoder::decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) {
  ASSERT(!complete_);
  complete_ = end_stream;
  headers_ = std::move(headers);
  if (complete_) {
    onComplete();
  }
}

void BufferingStreamDecoder::decodeData(const Buffer::Instance& data, bool end_stream) {
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

BufferingStreamDecoderPtr IntegrationUtil::makeSingleRequest(uint32_t port, std::string method,
                                                             std::string url,
                                                             Http::CodecClient::Type type,
                                                             std::string host) {
  Api::Impl api = Api::Impl(std::chrono::milliseconds(10000));
  Event::DispatcherPtr dispatcher(api.allocateDispatcher());
  Stats::IsolatedStoreImpl stats_store;
  Http::CodecClientStats stats{ALL_CODEC_CLIENT_STATS(POOL_COUNTER(stats_store))};
  Http::CodecClientProd client(
      type, dispatcher->createClientConnection(fmt::format("tcp://127.0.0.1:{}", port)), stats,
      stats_store, 0);
  BufferingStreamDecoderPtr response(new BufferingStreamDecoder([&]() -> void { client.close(); }));
  Http::StreamEncoder& encoder = client.newStream(*response);
  encoder.getStream().addCallbacks(*response);

  Http::HeaderMapImpl headers;
  headers.addViaMoveValue(Http::Headers::get().Method, std::move(method));
  headers.addViaMoveValue(Http::Headers::get().Path, std::move(url));
  headers.addViaMoveValue(Http::Headers::get().Host, std::move(host));
  headers.addViaMoveValue(Http::Headers::get().Scheme, "http");
  encoder.encodeHeaders(headers, true);

  dispatcher->run(Event::Dispatcher::RunType::Block);
  return response;
}

RawConnectionDriver::RawConnectionDriver(uint32_t port, Buffer::Instance& initial_data,
                                         ReadCallback data_callback) {
  api_.reset(new Api::Impl(std::chrono::milliseconds(10000)));
  dispatcher_ = api_->allocateDispatcher();
  client_ = dispatcher_->createClientConnection(fmt::format("tcp://127.0.0.1:{}", port));
  client_->addReadFilter(Network::ReadFilterPtr{new ForwardingFilter(*this, data_callback)});
  client_->write(initial_data);
  client_->connect();
}

RawConnectionDriver::~RawConnectionDriver() {}

void RawConnectionDriver::run() { dispatcher_->run(Event::Dispatcher::RunType::Block); }

void RawConnectionDriver::close() { client_->close(Network::ConnectionCloseType::FlushWrite); }
