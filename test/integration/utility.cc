#include "utility.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"

#include "common/api/api_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

namespace Envoy {
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
IntegrationUtil::makeSingleRequest(const Network::Address::InstanceConstSharedPtr& addr,
                                   const std::string& method, const std::string& url,
                                   const std::string& body, Http::CodecClient::Type type,
                                   const std::string& host) {
  Api::Impl api(std::chrono::milliseconds(9000));
  Event::DispatcherPtr dispatcher(api.allocateDispatcher());
  std::shared_ptr<Upstream::MockClusterInfo> cluster{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostDescriptionConstSharedPtr host_description{
      Upstream::makeTestHostDescription(cluster, "tcp://127.0.0.1:80")};
  Http::CodecClientProd client(
      type,
      dispatcher->createClientConnection(addr, Network::Address::InstanceConstSharedPtr(),
                                         Network::Test::createRawBufferSocket(), nullptr),
      host_description, *dispatcher);
  BufferingStreamDecoderPtr response(new BufferingStreamDecoder([&]() -> void {
    client.close();
    dispatcher->exit();
  }));
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

BufferingStreamDecoderPtr IntegrationUtil::makeSingleRequest(
    uint32_t port, const std::string& method, const std::string& url, const std::string& body,
    Http::CodecClient::Type type, Network::Address::IpVersion ip_version, const std::string& host) {
  auto addr = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(ip_version), port));
  return makeSingleRequest(addr, method, url, body, type, host);
}

RawConnectionDriver::RawConnectionDriver(uint32_t port, Buffer::Instance& initial_data,
                                         ReadCallback data_callback,
                                         Network::Address::IpVersion version) {
  api_.reset(new Api::Impl(std::chrono::milliseconds(10000)));
  dispatcher_ = api_->allocateDispatcher();
  client_ = dispatcher_->createClientConnection(
      Network::Utility::resolveUrl(
          fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version), port)),
      Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(), nullptr);
  client_->addReadFilter(Network::ReadFilterSharedPtr{new ForwardingFilter(*this, data_callback)});
  client_->write(initial_data, false);
  client_->connect();
}

RawConnectionDriver::~RawConnectionDriver() {}

void RawConnectionDriver::run() { dispatcher_->run(Event::Dispatcher::RunType::Block); }

void RawConnectionDriver::close() { client_->close(Network::ConnectionCloseType::FlushWrite); }

WaitForPayloadReader::WaitForPayloadReader(Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

Network::FilterStatus WaitForPayloadReader::onData(Buffer::Instance& data, bool end_stream) {
  data_.append(TestUtility::bufferToString(data));
  data.drain(data.length());
  read_end_stream_ = end_stream;
  if ((!data_to_wait_for_.empty() && data_.find(data_to_wait_for_) == 0) || end_stream) {
    data_to_wait_for_.clear();
    dispatcher_.exit();
  }

  return Network::FilterStatus::StopIteration;
}

} // namespace Envoy
