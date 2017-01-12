#include <gtest/gtest.h>
#include "integration.h"

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/header_map.h"

#include "common/api/api_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

IntegrationTestServerPtr BaseIntegrationTest::test_server_;
std::vector<std::unique_ptr<FakeUpstream>> BaseIntegrationTest::fake_upstreams_;
spdlog::level::level_enum BaseIntegrationTest::default_log_level_;

IntegrationStreamDecoder::IntegrationStreamDecoder(Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

void IntegrationStreamDecoder::waitForBodyData(uint64_t size) {
  ASSERT(body_data_waiting_length_ == 0);
  body_data_waiting_length_ = size;
  dispatcher_.run(Event::Dispatcher::RunType::Block);
}

void IntegrationStreamDecoder::waitForEndStream() {
  if (!saw_end_stream_) {
    waiting_for_end_stream_ = true;
    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }
}

void IntegrationStreamDecoder::waitForReset() {
  if (!saw_reset_) {
    waiting_for_reset_ = true;
    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }
}

void IntegrationStreamDecoder::decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) {
  saw_end_stream_ = end_stream;
  headers_ = std::move(headers);
  if (end_stream && waiting_for_end_stream_) {
    dispatcher_.exit();
  }
}

void IntegrationStreamDecoder::decodeData(Buffer::Instance& data, bool end_stream) {
  saw_end_stream_ = end_stream;
  uint64_t num_slices = data.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  data.getRawSlices(slices, num_slices);
  for (Buffer::RawSlice& slice : slices) {
    body_.append(static_cast<const char*>(slice.mem_), slice.len_);
  }

  if (end_stream && waiting_for_end_stream_) {
    dispatcher_.exit();
  } else if (body_data_waiting_length_ > 0) {
    body_data_waiting_length_ -= std::min(body_data_waiting_length_, data.length());
    if (body_data_waiting_length_ == 0) {
      dispatcher_.exit();
    }
  }
}

void IntegrationStreamDecoder::decodeTrailers(Http::HeaderMapPtr&& trailers) {
  saw_end_stream_ = true;
  trailers_ = std::move(trailers);
  if (waiting_for_end_stream_) {
    dispatcher_.exit();
  }
}

void IntegrationStreamDecoder::onResetStream(Http::StreamResetReason) {
  saw_reset_ = true;
  if (waiting_for_reset_) {
    dispatcher_.exit();
  }
}

IntegrationCodecClient::IntegrationCodecClient(Event::Dispatcher& dispatcher,
                                               Network::ClientConnectionPtr&& conn,
                                               Upstream::HostDescriptionPtr host_description,
                                               CodecClient::Type type)
    : CodecClientProd(type, std::move(conn), host_description), callbacks_(*this),
      codec_callbacks_(*this) {
  connection_->addConnectionCallbacks(callbacks_);
  setCodecConnectionCallbacks(codec_callbacks_);
  dispatcher.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(connected_);
}

void IntegrationCodecClient::flushWrite() {
  connection_->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
  // NOTE: We should run blocking until all the body data is flushed.
}

void IntegrationCodecClient::makeHeaderOnlyRequest(const Http::HeaderMap& headers,
                                                   IntegrationStreamDecoder& response) {
  Http::StreamEncoder& encoder = newStream(response);
  encoder.getStream().addCallbacks(response);
  encoder.encodeHeaders(headers, true);
  flushWrite();
}

void IntegrationCodecClient::makeRequestWithBody(const Http::HeaderMap& headers, uint64_t body_size,
                                                 IntegrationStreamDecoder& response) {
  Http::StreamEncoder& encoder = newStream(response);
  encoder.getStream().addCallbacks(response);
  encoder.encodeHeaders(headers, false);
  Buffer::OwnedImpl data(std::string(body_size, 'a'));
  encoder.encodeData(data, true);
  flushWrite();
}

void IntegrationCodecClient::sendData(Http::StreamEncoder& encoder, uint64_t size,
                                      bool end_stream) {
  Buffer::OwnedImpl data(std::string(size, 'a'));
  encoder.encodeData(data, end_stream);
  flushWrite();
}

void IntegrationCodecClient::sendTrailers(Http::StreamEncoder& encoder,
                                          const Http::HeaderMap& trailers) {
  encoder.encodeTrailers(trailers);
  flushWrite();
}

void IntegrationCodecClient::sendReset(Http::StreamEncoder& encoder) {
  encoder.getStream().resetStream(Http::StreamResetReason::LocalReset);
  flushWrite();
}

Http::StreamEncoder& IntegrationCodecClient::startRequest(const Http::HeaderMap& headers,
                                                          IntegrationStreamDecoder& response) {
  Http::StreamEncoder& encoder = newStream(response);
  encoder.getStream().addCallbacks(response);
  encoder.encodeHeaders(headers, false);
  flushWrite();
  return encoder;
}

void IntegrationCodecClient::waitForDisconnect() {
  connection_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(disconnected_);
}

void IntegrationCodecClient::ConnectionCallbacks::onEvent(uint32_t events) {
  if (events & Network::ConnectionEvent::Connected) {
    parent_.connected_ = true;
    parent_.connection_->dispatcher().exit();
  } else if (events & Network::ConnectionEvent::RemoteClose) {
    parent_.disconnected_ = true;
    parent_.connection_->dispatcher().exit();
  }
}

IntegrationTcpClient::IntegrationTcpClient(Event::Dispatcher& dispatcher, uint32_t port)
    : callbacks_(new ConnectionCallbacks(*this)) {
  connection_ = dispatcher.createClientConnection(fmt::format("tcp://127.0.0.1:{}", port));
  connection_->addConnectionCallbacks(*callbacks_);
  connection_->addReadFilter(callbacks_);
  connection_->connect();
}

void IntegrationTcpClient::close() { connection_->close(Network::ConnectionCloseType::NoFlush); }

void IntegrationTcpClient::waitForData(const std::string& data) {
  if (data_.find(data) == 0) {
    return;
  }

  data_to_wait_for_ = data;
  connection_->dispatcher().run(Event::Dispatcher::RunType::Block);
}

void IntegrationTcpClient::waitForDisconnect() {
  connection_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(disconnected_);
}

void IntegrationTcpClient::write(const std::string& data) {
  Buffer::OwnedImpl buffer(data);
  connection_->write(buffer);
  connection_->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
  // NOTE: We should run blocking until all the body data is flushed.
}

Network::FilterStatus IntegrationTcpClient::ConnectionCallbacks::onData(Buffer::Instance& data) {
  parent_.data_.append(TestUtility::bufferToString(data));
  data.drain(data.length());
  if (!parent_.data_to_wait_for_.empty() && parent_.data_.find(parent_.data_to_wait_for_) == 0) {
    parent_.data_to_wait_for_.clear();
    parent_.connection_->dispatcher().exit();
  }

  return Network::FilterStatus::StopIteration;
}

void IntegrationTcpClient::ConnectionCallbacks::onEvent(uint32_t events) {
  if (events == Network::ConnectionEvent::RemoteClose) {
    parent_.disconnected_ = true;
    parent_.connection_->dispatcher().exit();
  }
}

BaseIntegrationTest::BaseIntegrationTest()
    : api_(new Api::Impl(std::chrono::milliseconds(10000))),
      dispatcher_(api_->allocateDispatcher()) {

  // This is a hack, but there are situations where we disconnect fake upstream connections and
  // then we expect the server connection pool to get the disconnect before the next test starts.
  // This does not always happen. This pause should allow the server to pick up the disconnect
  // notification and clear the pool connection if necessary. A real fix would require adding fairly
  // complex test hooks to the server and/or spin waiting on stats, neither of which I think are
  // necessary right now.
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

BaseIntegrationTest::~BaseIntegrationTest() {}

Network::ClientConnectionPtr BaseIntegrationTest::makeClientConnection(uint32_t port) {
  return dispatcher_->createClientConnection(fmt::format("tcp://127.0.0.1:{}", port));
}

IntegrationCodecClientPtr BaseIntegrationTest::makeHttpConnection(uint32_t port,
                                                                  Http::CodecClient::Type type) {
  return makeHttpConnection(makeClientConnection(port), type);
}

IntegrationCodecClientPtr
BaseIntegrationTest::makeHttpConnection(Network::ClientConnectionPtr&& conn,
                                        Http::CodecClient::Type type) {
  std::shared_ptr<Upstream::MockClusterInfo> cluster{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostDescriptionPtr host_description{
      new Upstream::HostDescriptionImpl(cluster, "tcp://127.0.0.1:80", false, "")};
  return IntegrationCodecClientPtr{
      new IntegrationCodecClient(*dispatcher_, std::move(conn), host_description, type)};
}

IntegrationTcpClientPtr BaseIntegrationTest::makeTcpConnection(uint32_t port) {
  return IntegrationTcpClientPtr{new IntegrationTcpClient(*dispatcher_, port)};
}
