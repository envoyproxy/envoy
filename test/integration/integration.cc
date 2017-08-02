#include "test/integration/integration.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/header_map.h"

#include "common/api/api_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/network/connection_impl.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/integration/utility.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

using testing::AnyNumber;
using testing::Invoke;
using testing::_;

namespace Envoy {
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

void IntegrationStreamDecoder::onResetStream(Http::StreamResetReason reason) {
  saw_reset_ = true;
  reset_reason_ = reason;
  if (waiting_for_reset_) {
    dispatcher_.exit();
  }
}

IntegrationCodecClient::IntegrationCodecClient(
    Event::Dispatcher& dispatcher, Network::ClientConnectionPtr&& conn,
    Upstream::HostDescriptionConstSharedPtr host_description, CodecClient::Type type)
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

void IntegrationCodecClient::sendData(Http::StreamEncoder& encoder, Buffer::Instance& data,
                                      bool end_stream) {
  encoder.encodeData(data, end_stream);
  flushWrite();
}

void IntegrationCodecClient::sendData(Http::StreamEncoder& encoder, uint64_t size,
                                      bool end_stream) {
  Buffer::OwnedImpl data(std::string(size, 'a'));
  sendData(encoder, data, end_stream);
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

void IntegrationCodecClient::ConnectionCallbacks::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected) {
    parent_.connected_ = true;
    parent_.connection_->dispatcher().exit();
  } else if (event == Network::ConnectionEvent::RemoteClose) {
    parent_.disconnected_ = true;
    parent_.connection_->dispatcher().exit();
  }
}

IntegrationTcpClient::IntegrationTcpClient(Event::Dispatcher& dispatcher,
                                           MockBufferFactory& factory, uint32_t port,
                                           Network::Address::IpVersion version)
    : payload_reader_(new WaitForPayloadReader(dispatcher)),
      callbacks_(new ConnectionCallbacks(*this)) {
  EXPECT_CALL(factory, create_())
      .Times(2)
      .WillOnce(Invoke([&]() -> Buffer::Instance* {
        return new Buffer::OwnedImpl; // client read buffer.
      }))
      .WillOnce(Invoke([&]() -> Buffer::Instance* {
        client_write_buffer_ = new MockBuffer;
        return client_write_buffer_;
      }));

  connection_ = dispatcher.createClientConnection(Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version), port)));

  ON_CALL(*client_write_buffer_, drain(_))
      .WillByDefault(testing::Invoke(client_write_buffer_, &MockBuffer::baseDrain));
  EXPECT_CALL(*client_write_buffer_, drain(_)).Times(AnyNumber());

  connection_->addConnectionCallbacks(*callbacks_);
  connection_->addReadFilter(payload_reader_);
  connection_->connect();
}

void IntegrationTcpClient::close() { connection_->close(Network::ConnectionCloseType::NoFlush); }

void IntegrationTcpClient::waitForData(const std::string& data) {
  if (payload_reader_->data().find(data) == 0) {
    return;
  }

  payload_reader_->set_data_to_wait_for(data);
  connection_->dispatcher().run(Event::Dispatcher::RunType::Block);
}

void IntegrationTcpClient::waitForDisconnect() {
  connection_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(disconnected_);
}

void IntegrationTcpClient::write(const std::string& data) {
  Buffer::OwnedImpl buffer(data);
  EXPECT_CALL(*client_write_buffer_, move(_)).Times(1);
  EXPECT_CALL(*client_write_buffer_, write(_)).Times(1);

  int bytes_expected = client_write_buffer_->bytes_written() + data.size();

  connection_->write(buffer);
  while (client_write_buffer_->bytes_written() != bytes_expected) {
    connection_->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
  }
}

void IntegrationTcpClient::ConnectionCallbacks::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose) {
    parent_.disconnected_ = true;
    parent_.connection_->dispatcher().exit();
  }
}

BaseIntegrationTest::BaseIntegrationTest(Network::Address::IpVersion version)
    : api_(new Api::Impl(std::chrono::milliseconds(10000))),
      mock_buffer_factory_(new NiceMock<MockBufferFactory>),
      dispatcher_(new Event::DispatcherImpl(Buffer::FactoryPtr{mock_buffer_factory_})),
      default_log_level_(TestEnvironment::getOptions().logLevel()), version_(version) {
  // This is a hack, but there are situations where we disconnect fake upstream connections and
  // then we expect the server connection pool to get the disconnect before the next test starts.
  // This does not always happen. This pause should allow the server to pick up the disconnect
  // notification and clear the pool connection if necessary. A real fix would require adding fairly
  // complex test hooks to the server and/or spin waiting on stats, neither of which I think are
  // necessary right now.
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  ON_CALL(*mock_buffer_factory_, create_()).WillByDefault(Invoke([&]() -> Buffer::Instance* {
    return new Buffer::OwnedImpl;
  }));
}

Network::ClientConnectionPtr BaseIntegrationTest::makeClientConnection(uint32_t port) {
  return dispatcher_->createClientConnection(Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port)));
}

IntegrationCodecClientPtr BaseIntegrationTest::makeHttpConnection(uint32_t port,
                                                                  Http::CodecClient::Type type) {
  return makeHttpConnection(makeClientConnection(port), type);
}

IntegrationCodecClientPtr
BaseIntegrationTest::makeHttpConnection(Network::ClientConnectionPtr&& conn,
                                        Http::CodecClient::Type type) {
  std::shared_ptr<Upstream::MockClusterInfo> cluster{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostDescriptionConstSharedPtr host_description{new Upstream::HostDescriptionImpl(
      cluster, "",
      Network::Utility::resolveUrl(
          fmt::format("tcp://{}:80", Network::Test::getLoopbackAddressUrlString(version_))),
      false, "")};
  return IntegrationCodecClientPtr{
      new IntegrationCodecClient(*dispatcher_, std::move(conn), host_description, type)};
}

IntegrationTcpClientPtr BaseIntegrationTest::makeTcpConnection(uint32_t port) {
  return IntegrationTcpClientPtr{
      new IntegrationTcpClient(*dispatcher_, *mock_buffer_factory_, port, version_)};
}

void BaseIntegrationTest::registerPort(const std::string& key, uint32_t port) {
  port_map_[key] = port;
}

uint32_t BaseIntegrationTest::lookupPort(const std::string& key) {
  auto it = port_map_.find(key);
  if (it != port_map_.end()) {
    return it->second;
  }
  RELEASE_ASSERT(false);
}

void BaseIntegrationTest::registerTestServerPorts(const std::vector<std::string>& port_names) {
  auto port_it = port_names.cbegin();
  auto listeners = test_server_->server().listenerManager().listeners();
  auto listener_it = listeners.cbegin();
  for (; port_it != port_names.end() && listener_it != listeners.end(); ++port_it, ++listener_it) {
    registerPort(*port_it, listener_it->get().socket().localAddress()->ip()->port());
  }
  registerPort("admin", test_server_->server().admin().socket().localAddress()->ip()->port());
}

void BaseIntegrationTest::createTestServer(const std::string& json_path,
                                           const std::vector<std::string>& port_names) {
  test_server_ = IntegrationTestServer::create(
      TestEnvironment::temporaryFileSubstitute(json_path, port_map_, version_), version_);
  registerTestServerPorts(port_names);
}

void BaseIntegrationTest::testRouterRequestAndResponseWithBody(Network::ClientConnectionPtr&& conn,
                                                               Http::CodecClient::Type type,
                                                               uint64_t request_size,
                                                               uint64_t response_size,
                                                               bool big_header) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  executeActions(
      {[&]() -> void { codec_client = makeHttpConnection(std::move(conn), type); },
       [&]() -> void {
         Http::TestHeaderMapImpl headers{
             {":method", "POST"},    {":path", "/test/long/url"}, {":scheme", "http"},
             {":authority", "host"}, {"x-lyft-user-id", "123"},   {"x-forwarded-for", "10.0.0.1"}};
         if (big_header) {
           headers.addViaCopy("big", std::string(4096, 'a'));
         }

         codec_client->makeRequestWithBody(headers, request_size, *response);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         request->encodeData(response_size, true);
       },
       [&]() -> void { response->waitForEndStream(); },
       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});

  EXPECT_TRUE(request->complete());
  EXPECT_EQ(request_size, request->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(response_size, response->body().size());
}

void BaseIntegrationTest::testRouterHeaderOnlyRequestAndResponse(
    Network::ClientConnectionPtr&& conn, Http::CodecClient::Type type, bool close_upstream) {

  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  executeActions(
      {[&]() -> void { codec_client = makeHttpConnection(std::move(conn), type); },
       [&]() -> void {
         codec_client->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                     {":path", "/test/long/url"},
                                                                     {":scheme", "http"},
                                                                     {":authority", "host"},
                                                                     {"x-lyft-user-id", "123"}},
                                             *response);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
       },
       [&]() -> void { response->waitForEndStream(); },
       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); }});

  // The following allows us to test shutting down the server with active connection pool
  // connections. Either way we need to clean up the upstream connections to avoid race conditions.
  if (!close_upstream) {
    test_server_.reset();
  }

  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();

  EXPECT_TRUE(request->complete());
  EXPECT_EQ(0U, request->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(0U, response->body().size());
}

void BaseIntegrationTest::testRouterNotFound(Http::CodecClient::Type type) {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/notfound", "", type, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
}

void BaseIntegrationTest::testRouterNotFoundWithBody(uint32_t port, Http::CodecClient::Type type) {
  BufferingStreamDecoderPtr response =
      IntegrationUtil::makeSingleRequest(port, "POST", "/notfound", "foo", type, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
}

void BaseIntegrationTest::testRouterRedirect(Http::CodecClient::Type type) {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/foo", "", type, version_, "www.redirect.com");
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("301", response->headers().Status()->value().c_str());
  EXPECT_STREQ("https://www.redirect.com/foo",
               response->headers().get(Http::Headers::get().Location)->value().c_str());
}

void BaseIntegrationTest::testDrainClose(Http::CodecClient::Type type) {
  test_server_->drainManager().draining_ = true;

  IntegrationCodecClientPtr codec_client;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  executeActions({[&]() -> void { codec_client = makeHttpConnection(lookupPort("http"), type); },
                  [&]() -> void {
                    codec_client->makeHeaderOnlyRequest(
                        Http::TestHeaderMapImpl{{":method", "GET"},
                                                {":path", "/healthcheck"},
                                                {":scheme", "http"},
                                                {":authority", "host"}},
                        *response);
                  },
                  [&]() -> void { response->waitForEndStream(); },
                  [&]() -> void { codec_client->waitForDisconnect(); }});

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  if (type == Http::CodecClient::Type::HTTP2) {
    EXPECT_TRUE(codec_client->sawGoAway());
  }

  test_server_->drainManager().draining_ = false;
}

void BaseIntegrationTest::testRouterUpstreamDisconnectBeforeRequestComplete(
    Network::ClientConnectionPtr&& conn, Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  std::list<std::function<void()>> actions = {
      [&]() -> void { codec_client = makeHttpConnection(std::move(conn), type); },
      [&]() -> void {
        codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/test/long/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"}},
                                   *response);
      },
      [&]() -> void {
        fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
      },
      [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
      [&]() -> void { request->waitForHeadersComplete(); },
      [&]() -> void { fake_upstream_connection->close(); },
      [&]() -> void { fake_upstream_connection->waitForDisconnect(); },
      [&]() -> void { response->waitForEndStream(); }};

  if (type == Http::CodecClient::Type::HTTP1) {
    actions.push_back([&]() -> void { codec_client->waitForDisconnect(); });
  } else {
    actions.push_back([&]() -> void { codec_client->close(); });
  }

  executeActions(actions);

  EXPECT_FALSE(request->complete());
  EXPECT_EQ(0U, request->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
  EXPECT_EQ("upstream connect error or disconnect/reset before headers", response->body());
}

void BaseIntegrationTest::testRouterUpstreamDisconnectBeforeResponseComplete(
    Network::ClientConnectionPtr&& conn, Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  std::list<std::function<void()>> actions = {
      [&]() -> void { codec_client = makeHttpConnection(std::move(conn), type); },
      [&]() -> void {
        codec_client->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                    {":path", "/test/long/url"},
                                                                    {":scheme", "http"},
                                                                    {":authority", "host"}},
                                            *response);
      },
      [&]() -> void {
        fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
      },
      [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
      [&]() -> void { request->waitForEndStream(*dispatcher_); },
      [&]() -> void {
        request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
      },
      [&]() -> void { fake_upstream_connection->close(); },
      [&]() -> void { fake_upstream_connection->waitForDisconnect(); }};

  if (type == Http::CodecClient::Type::HTTP1) {
    actions.push_back([&]() -> void { codec_client->waitForDisconnect(); });
  } else {
    actions.push_back([&]() -> void { response->waitForReset(); });
    actions.push_back([&]() -> void { codec_client->close(); });
  }

  executeActions(actions);

  EXPECT_TRUE(request->complete());
  EXPECT_EQ(0U, request->bodyLength());

  EXPECT_FALSE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(0U, response->body().size());
}

void BaseIntegrationTest::testRouterDownstreamDisconnectBeforeRequestComplete(
    Network::ClientConnectionPtr&& conn, Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  std::list<std::function<void()>> actions = {
      [&]() -> void { codec_client = makeHttpConnection(std::move(conn), type); },
      [&]() -> void {
        codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/test/long/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"}},
                                   *response);
      },
      [&]() -> void {
        fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
      },
      [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
      [&]() -> void { request->waitForHeadersComplete(); },
      [&]() -> void { codec_client->close(); }};

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  } else {
    actions.push_back([&]() -> void { request->waitForReset(); });
    actions.push_back([&]() -> void { fake_upstream_connection->close(); });
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  }

  executeActions(actions);

  EXPECT_FALSE(request->complete());
  EXPECT_EQ(0U, request->bodyLength());

  EXPECT_FALSE(response->complete());
}

void BaseIntegrationTest::testRouterDownstreamDisconnectBeforeResponseComplete(
    Network::ClientConnectionPtr&& conn, Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  std::list<std::function<void()>> actions = {
      [&]() -> void { codec_client = makeHttpConnection(std::move(conn), type); },
      [&]() -> void {
        codec_client->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                    {":path", "/test/long/url"},
                                                                    {":scheme", "http"},
                                                                    {":authority", "host"}},
                                            *response);
      },
      [&]() -> void {
        fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
      },
      [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
      [&]() -> void { request->waitForEndStream(*dispatcher_); },
      [&]() -> void {
        request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
        request->encodeData(512, false);
      },
      [&]() -> void { response->waitForBodyData(512); },
      [&]() -> void { codec_client->close(); }};

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  } else {
    actions.push_back([&]() -> void { request->waitForReset(); });
    actions.push_back([&]() -> void { fake_upstream_connection->close(); });
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  }

  executeActions(actions);

  EXPECT_TRUE(request->complete());
  EXPECT_EQ(0U, request->bodyLength());

  EXPECT_FALSE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

void BaseIntegrationTest::testRouterUpstreamResponseBeforeRequestComplete(
    Network::ClientConnectionPtr&& conn, Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  std::list<std::function<void()>> actions = {
      [&]() -> void { codec_client = makeHttpConnection(std::move(conn), type); },
      [&]() -> void {
        codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/test/long/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"}},
                                   *response);
      },
      [&]() -> void {
        fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
      },
      [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
      [&]() -> void { request->waitForHeadersComplete(); },
      [&]() -> void {
        request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
        request->encodeData(512, true);
      },
      [&]() -> void { response->waitForEndStream(); }};

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  } else {
    actions.push_back([&]() -> void { request->waitForReset(); });
    actions.push_back([&]() -> void { fake_upstream_connection->close(); });
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  }

  if (type == Http::CodecClient::Type::HTTP1) {
    actions.push_back([&]() -> void { codec_client->waitForDisconnect(); });
  } else {
    actions.push_back([&]() -> void { codec_client->close(); });
  }

  executeActions(actions);

  EXPECT_FALSE(request->complete());
  EXPECT_EQ(0U, request->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

void BaseIntegrationTest::testRetry(Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  executeActions(
      {[&]() -> void { codec_client = makeHttpConnection(lookupPort("http"), type); },
       [&]() -> void {
         codec_client->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/test/long/url"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "host"},
                                                                   {"x-forwarded-for", "10.0.0.1"},
                                                                   {"x-envoy-retry-on", "5xx"}},
                                           1024, *response);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);
       },
       [&]() -> void {
         if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
           fake_upstream_connection->waitForDisconnect();
           fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
         } else {
           request->waitForReset();
         }
       },
       [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         request->encodeData(512, true);
       },
       [&]() -> void {
         response->waitForEndStream();
         EXPECT_TRUE(request->complete());
         EXPECT_EQ(1024U, request->bodyLength());

         EXPECT_TRUE(response->complete());
         EXPECT_STREQ("200", response->headers().Status()->value().c_str());
         EXPECT_EQ(512U, response->body().size());
       },
       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});
}

void BaseIntegrationTest::testGrpcRetry() {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  Http::TestHeaderMapImpl response_trailers{{"response1", "trailer1"}, {"grpc-status", "0"}};
  executeActions(
      {[&]() -> void {
         codec_client = makeHttpConnection(lookupPort("http"), Http::CodecClient::Type::HTTP2);
       },
       [&]() -> void {
         Http::StreamEncoder* request_encoder;
         request_encoder = &codec_client->startRequest(
             Http::TestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-grpc-on", "cancelled"}},
             *response);
         codec_client->sendData(*request_encoder, 1024, true);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}, {"grpc-status", "1"}},
                                false);
       },
       [&]() -> void {
         if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
           fake_upstream_connection->waitForDisconnect();
           fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
         } else {
           request->waitForReset();
         }
       },
       [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         request->encodeData(512,
                             fake_upstreams_[0]->httpType() != FakeHttpConnection::Type::HTTP2);
         if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP2) {
           request->encodeTrailers(response_trailers);
         }
       },
       [&]() -> void {
         response->waitForEndStream();
         EXPECT_TRUE(request->complete());
         EXPECT_EQ(1024U, request->bodyLength());

         EXPECT_TRUE(response->complete());
         EXPECT_STREQ("200", response->headers().Status()->value().c_str());
         EXPECT_EQ(512U, response->body().size());
         if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP2) {
           EXPECT_THAT(*response->trailers(), HeaderMapEqualRef(&response_trailers));
         }
       },
       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});
}

void BaseIntegrationTest::testTwoRequests(Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  executeActions(
      {[&]() -> void { codec_client = makeHttpConnection(lookupPort("http"), type); },
       // Request 1.
       [&]() -> void {
         codec_client->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/test/long/url"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "host"}},
                                           1024, *response);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         request->encodeData(512, true);
       },
       [&]() -> void {
         response->waitForEndStream();
         EXPECT_TRUE(request->complete());
         EXPECT_EQ(1024U, request->bodyLength());

         EXPECT_TRUE(response->complete());
         EXPECT_STREQ("200", response->headers().Status()->value().c_str());
         EXPECT_EQ(512U, response->body().size());
       },
       // Request 2.
       [&]() -> void {
         response.reset(new IntegrationStreamDecoder(*dispatcher_));
         codec_client->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/test/long/url"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "host"}},
                                           512, *response);
       },
       [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         request->encodeData(1024, true);
       },
       [&]() -> void {
         response->waitForEndStream();
         EXPECT_TRUE(request->complete());
         EXPECT_EQ(512U, request->bodyLength());

         EXPECT_TRUE(response->complete());
         EXPECT_STREQ("200", response->headers().Status()->value().c_str());
         EXPECT_EQ(1024U, response->body().size());
       },
       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});
}

void BaseIntegrationTest::sendRawHttpAndWaitForResponse(const char* raw_http,
                                                        std::string* response) {
  Buffer::OwnedImpl buffer(raw_http);
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response->append(TestUtility::bufferToString(data));
      },
      version_);

  connection.run();
}

void BaseIntegrationTest::testBadFirstline() {
  std::string response;
  sendRawHttpAndWaitForResponse("hello", &response);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", response);
}

void BaseIntegrationTest::testMissingDelimiter() {
  std::string response;
  sendRawHttpAndWaitForResponse("GET / HTTP/1.1\r\nHost: host\r\nfoo bar\r\n\r\n", &response);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", response);
}

void BaseIntegrationTest::testInvalidCharacterInFirstline() {
  std::string response;
  sendRawHttpAndWaitForResponse("GE(T / HTTP/1.1\r\nHost: host\r\n\r\n", &response);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", response);
}

void BaseIntegrationTest::testLowVersion() {
  std::string response;
  sendRawHttpAndWaitForResponse("GET / HTTP/0.8\r\nHost: host\r\n\r\n", &response);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", response);
}

void BaseIntegrationTest::testHttp10Request() {
  Buffer::OwnedImpl buffer("GET / HTTP/1.0\r\n\r\n");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
        client.close(Network::ConnectionCloseType::NoFlush);
      },
      version_);

  connection.run();
  EXPECT_TRUE(response.find("HTTP/1.1 426 Upgrade Required\r\n") == 0);
}

void BaseIntegrationTest::testNoHost() {
  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
        client.close(Network::ConnectionCloseType::NoFlush);
      },
      version_);

  connection.run();
  EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request\r\n") == 0);
}

void BaseIntegrationTest::testAbsolutePath() {
  Buffer::OwnedImpl buffer("GET http://www.redirect.com HTTP/1.1\r\nHost: host\r\n\r\n");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http_forward"), buffer,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
        client.close(Network::ConnectionCloseType::NoFlush);
      },
      version_);

  connection.run();
  EXPECT_FALSE(response.find("HTTP/1.1 404 Not Found\r\n") == 0);
}

void BaseIntegrationTest::testAllowAbsoluteSameRelative() {
  // Ensure that relative urls behave the same with allow_absolute_url enabled and without
  testEquivalent("GET /foo/bar HTTP/1.1\r\nHost: host\r\n\r\n");
}

void BaseIntegrationTest::testConnect() {
  // Ensure that connect behaves the same with allow_absolute_url enabled and without
  testEquivalent("CONNECT www.somewhere.com:80 HTTP/1.1\r\nHost: host\r\n\r\n");
}

void BaseIntegrationTest::testEquivalent(const std::string& request) {
  Buffer::OwnedImpl buffer1(request);
  std::string response1;
  RawConnectionDriver connection1(
      lookupPort("http"), buffer1,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response1.append(TestUtility::bufferToString(data));
        client.close(Network::ConnectionCloseType::NoFlush);
      },
      version_);

  connection1.run();

  Buffer::OwnedImpl buffer2(request);
  std::string response2;
  RawConnectionDriver connection2(
      lookupPort("http_forward"), buffer2,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response2.append(TestUtility::bufferToString(data));
        client.close(Network::ConnectionCloseType::NoFlush);
      },
      version_);

  connection2.run();

  EXPECT_TRUE(response1 == response2);
}

void BaseIntegrationTest::testBadPath() {
  Buffer::OwnedImpl buffer("GET http://api.lyft.com HTTP/1.1\r\nHost: host\r\n\r\n");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
        client.close(Network::ConnectionCloseType::NoFlush);
      },
      version_);

  connection.run();
  EXPECT_TRUE(response.find("HTTP/1.1 404 Not Found\r\n") == 0);
}

void BaseIntegrationTest::testValidZeroLengthContent(Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeHttpConnectionPtr fake_upstream_connection;
  FakeStreamPtr request;
  executeActions(
      {[&]() -> void { codec_client = makeHttpConnection(lookupPort("http"), type); },
       [&]() -> void {
         codec_client->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                     {":path", "/test/long/url"},
                                                                     {":scheme", "http"},
                                                                     {":authority", "host"},
                                                                     {"content-length", "0"}},
                                             *response);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
       },
       [&]() -> void { response->waitForEndStream(); },
       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});

  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

void BaseIntegrationTest::testInvalidContentLength(Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  executeActions({[&]() -> void { codec_client = makeHttpConnection(lookupPort("http"), type); },
                  [&]() -> void {
                    codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                       {":path", "/test/long/url"},
                                                                       {":authority", "host"},
                                                                       {"content-length", "-1"}},
                                               *response);
                  },
                  [&]() -> void {
                    if (type == Http::CodecClient::Type::HTTP1) {
                      codec_client->waitForDisconnect();
                    } else {
                      response->waitForReset();
                      codec_client->close();
                    }
                  }});

  if (type == Http::CodecClient::Type::HTTP1) {
    ASSERT_TRUE(response->complete());
    EXPECT_STREQ("400", response->headers().Status()->value().c_str());
  } else {
    ASSERT_TRUE(response->reset());
    EXPECT_EQ(Http::StreamResetReason::RemoteReset, response->reset_reason());
  }
}

void BaseIntegrationTest::testMultipleContentLengths(Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  executeActions({[&]() -> void { codec_client = makeHttpConnection(lookupPort("http"), type); },
                  [&]() -> void {
                    codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                       {":path", "/test/long/url"},
                                                                       {":authority", "host"},
                                                                       {"content-length", "3,2"}},
                                               *response);
                  },
                  [&]() -> void {
                    if (type == Http::CodecClient::Type::HTTP1) {
                      codec_client->waitForDisconnect();
                    } else {
                      response->waitForReset();
                      codec_client->close();
                    }
                  }});

  if (type == Http::CodecClient::Type::HTTP1) {
    ASSERT_TRUE(response->complete());
    EXPECT_STREQ("400", response->headers().Status()->value().c_str());
  } else {
    ASSERT_TRUE(response->reset());
    EXPECT_EQ(Http::StreamResetReason::RemoteReset, response->reset_reason());
  }
}

void BaseIntegrationTest::testOverlyLongHeaders(Http::CodecClient::Type type) {
  Http::TestHeaderMapImpl big_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  big_headers.addViaCopy("big", std::string(60 * 1024, 'a'));
  IntegrationCodecClientPtr codec_client;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  executeActions({[&]() -> void { codec_client = makeHttpConnection(lookupPort("http"), type); },
                  [&]() -> void {
                    std::string long_value(7500, 'x');
                    codec_client->startRequest(big_headers, *response);
                  },
                  [&]() -> void { codec_client->waitForDisconnect(); }});

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("431", response->headers().Status()->value().c_str());
}

void BaseIntegrationTest::testUpstreamProtocolError() {
  IntegrationCodecClientPtr codec_client;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeRawConnectionPtr fake_upstream_connection;
  executeActions(
      {[&]() -> void {
         codec_client = makeHttpConnection(lookupPort("http"), Http::CodecClient::Type::HTTP1);
       },
       [&]() -> void {
         codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                            {":path", "/test/long/url"},
                                                            {":authority", "host"}},
                                    *response);
       },
       [&]() -> void { fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection(); },
       // TODO(mattklein123): Waiting for exact amount of data is a hack. This needs to
       // be fixed.
       [&]() -> void { fake_upstream_connection->waitForData(187); },
       [&]() -> void { fake_upstream_connection->write("bad protocol data!"); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); },
       [&]() -> void { codec_client->waitForDisconnect(); }});

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
}

void BaseIntegrationTest::testDownstreamResetBeforeResponseComplete() {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  Http::StreamEncoder* downstream_request{};
  FakeStreamPtr upstream_request;
  std::list<std::function<void()>> actions = {
      [&]() -> void {
        codec_client = makeHttpConnection(lookupPort("http"), Http::CodecClient::Type::HTTP2);
      },
      [&]() -> void {
        downstream_request =
            &codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                {":path", "/test/long/url"},
                                                                {":scheme", "http"},
                                                                {":authority", "host"},
                                                                {"cookie", "a=b"},
                                                                {"cookie", "c=d"}},
                                        *response);
        codec_client->sendData(*downstream_request, 0, true);
      },
      [&]() -> void {
        fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
      },
      [&]() -> void { upstream_request = fake_upstream_connection->waitForNewStream(); },
      [&]() -> void {
        upstream_request->waitForEndStream(*dispatcher_);
        EXPECT_EQ(upstream_request->headers().get(Http::Headers::get().Cookie)->value(),
                  "a=b; c=d");
      },
      [&]() -> void {
        upstream_request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
        upstream_request->encodeData(512, false);
      },
      [&]() -> void { response->waitForBodyData(512); },
      [&]() -> void { codec_client->sendReset(*downstream_request); }};

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  } else {
    actions.push_back([&]() -> void { upstream_request->waitForReset(); });
    actions.push_back([&]() -> void { fake_upstream_connection->close(); });
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  }

  actions.push_back([&]() -> void { codec_client->close(); });
  executeActions(actions);

  EXPECT_TRUE(upstream_request->complete());
  EXPECT_EQ(0U, upstream_request->bodyLength());

  EXPECT_FALSE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

void BaseIntegrationTest::testTrailers(uint64_t request_size, uint64_t response_size) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  Http::StreamEncoder* request_encoder;
  FakeStreamPtr upstream_request;
  Http::TestHeaderMapImpl request_trailers{{"request1", "trailer1"}, {"request2", "trailer2"}};
  Http::TestHeaderMapImpl response_trailers{{"response1", "trailer1"}, {"response2", "trailer2"}};
  executeActions(
      {[&]() -> void {
         codec_client =
             makeHttpConnection(lookupPort("http_buffer"), Http::CodecClient::Type::HTTP2);
       },
       [&]() -> void {
         request_encoder =
             &codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}},
                                         *response);
         codec_client->sendData(*request_encoder, request_size, false);
         codec_client->sendTrailers(*request_encoder, request_trailers);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { upstream_request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { upstream_request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         upstream_request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         upstream_request->encodeData(response_size, false);
         upstream_request->encodeTrailers(response_trailers);
       },
       [&]() -> void { response->waitForEndStream(); },
       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});

  EXPECT_TRUE(upstream_request->complete());
  EXPECT_EQ(request_size, upstream_request->bodyLength());
  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP2) {
    EXPECT_THAT(*upstream_request->trailers(), HeaderMapEqualRef(&request_trailers));
  }

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(response_size, response->body().size());
  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP2) {
    EXPECT_THAT(*response->trailers(), HeaderMapEqualRef(&response_trailers));
  }
}
} // namespace Envoy
