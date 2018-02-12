#include "test/integration/integration.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"

#include "common/api/api_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/libevent.h"
#include "common/network/connection_impl.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

using testing::AnyNumber;
using testing::AtLeast;
using testing::Invoke;
using testing::NiceMock;
using testing::_;

namespace Envoy {

IntegrationStreamDecoder::IntegrationStreamDecoder(Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

void IntegrationStreamDecoder::waitForContinueHeaders() {
  if (!continue_headers_.get()) {
    waiting_for_continue_headers_ = true;
    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }
}

void IntegrationStreamDecoder::waitForHeaders() {
  if (!headers_.get()) {
    waiting_for_headers_ = true;
    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }
}

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

void IntegrationStreamDecoder::decode100ContinueHeaders(Http::HeaderMapPtr&& headers) {
  continue_headers_ = std::move(headers);
  if (waiting_for_continue_headers_) {
    dispatcher_.exit();
  }
}

void IntegrationStreamDecoder::decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) {
  saw_end_stream_ = end_stream;
  headers_ = std::move(headers);
  if ((end_stream && waiting_for_end_stream_) || waiting_for_headers_) {
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

IntegrationTcpClient::IntegrationTcpClient(Event::Dispatcher& dispatcher,
                                           MockBufferFactory& factory, uint32_t port,
                                           Network::Address::IpVersion version,
                                           bool enable_half_close)
    : payload_reader_(new WaitForPayloadReader(dispatcher)),
      callbacks_(new ConnectionCallbacks(*this)) {
  EXPECT_CALL(factory, create_(_, _))
      .WillOnce(Invoke([&](std::function<void()> below_low,
                           std::function<void()> above_high) -> Buffer::Instance* {
        client_write_buffer_ = new MockWatermarkBuffer(below_low, above_high);
        return client_write_buffer_;
      }));

  connection_ = dispatcher.createClientConnection(
      Network::Utility::resolveUrl(
          fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version), port)),
      Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(), nullptr);

  ON_CALL(*client_write_buffer_, drain(_))
      .WillByDefault(testing::Invoke(client_write_buffer_, &MockWatermarkBuffer::baseDrain));
  EXPECT_CALL(*client_write_buffer_, drain(_)).Times(AnyNumber());

  connection_->enableHalfClose(enable_half_close);
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

void IntegrationTcpClient::waitForHalfClose() {
  connection_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(payload_reader_->readLastByte());
}

void IntegrationTcpClient::readDisable(bool disabled) { connection_->readDisable(disabled); }

void IntegrationTcpClient::write(const std::string& data, bool end_stream) {
  Buffer::OwnedImpl buffer(data);
  EXPECT_CALL(*client_write_buffer_, move(_));
  if (!data.empty()) {
    EXPECT_CALL(*client_write_buffer_, write(_)).Times(AtLeast(1));
  }

  int bytes_expected = client_write_buffer_->bytes_written() + data.size();

  connection_->write(buffer, end_stream);
  do {
    connection_->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
  } while (client_write_buffer_->bytes_written() != bytes_expected);
}

void IntegrationTcpClient::ConnectionCallbacks::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose) {
    parent_.disconnected_ = true;
    parent_.connection_->dispatcher().exit();
  }
}

BaseIntegrationTest::BaseIntegrationTest(Network::Address::IpVersion version,
                                         const std::string& config)
    : api_(new Api::Impl(std::chrono::milliseconds(10000))),
      mock_buffer_factory_(new NiceMock<MockBufferFactory>),
      dispatcher_(new Event::DispatcherImpl(Buffer::WatermarkFactoryPtr{mock_buffer_factory_})),
      version_(version), config_helper_(version, config),
      default_log_level_(TestEnvironment::getOptions().logLevel()) {
  // This is a hack, but there are situations where we disconnect fake upstream connections and
  // then we expect the server connection pool to get the disconnect before the next test starts.
  // This does not always happen. This pause should allow the server to pick up the disconnect
  // notification and clear the pool connection if necessary. A real fix would require adding fairly
  // complex test hooks to the server and/or spin waiting on stats, neither of which I think are
  // necessary right now.
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  ON_CALL(*mock_buffer_factory_, create_(_, _))
      .WillByDefault(Invoke([](std::function<void()> below_low,
                               std::function<void()> above_high) -> Buffer::Instance* {
        return new Buffer::WatermarkBuffer(below_low, above_high);
      }));
}

Network::ClientConnectionPtr BaseIntegrationTest::makeClientConnection(uint32_t port) {
  Network::ClientConnectionPtr connection(dispatcher_->createClientConnection(
      Network::Utility::resolveUrl(
          fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port)),
      Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(), nullptr));

  connection->enableHalfClose(enable_half_close_);
  return connection;
}

void BaseIntegrationTest::initialize() {
  RELEASE_ASSERT(!initialized_);
  RELEASE_ASSERT(Event::Libevent::Global::initialized());
  initialized_ = true;

  createUpstreams();
  createEnvoy();
}

void BaseIntegrationTest::createUpstreams() {
  if (autonomous_upstream_) {
    fake_upstreams_.emplace_back(new AutonomousUpstream(0, upstream_protocol_, version_));
  } else {
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, upstream_protocol_, version_, enable_half_close_));
  }
}

void BaseIntegrationTest::createEnvoy() {
  std::vector<uint32_t> ports;
  for (auto& upstream : fake_upstreams_) {
    if (upstream->localAddress()->ip()) {
      ports.push_back(upstream->localAddress()->ip()->port());
    }
  }
  config_helper_.finalize(ports);

  ENVOY_LOG_MISC(debug, "Running Envoy with configuration {}",
                 config_helper_.bootstrap().DebugString());

  const std::string bootstrap_path = TestEnvironment::writeStringToFileForTest(
      "bootstrap.json", MessageUtil::getJsonStringFromMessage(config_helper_.bootstrap()));

  std::vector<std::string> named_ports;
  const auto& static_resources = config_helper_.bootstrap().static_resources();
  for (int i = 0; i < static_resources.listeners_size(); ++i) {
    named_ports.push_back(static_resources.listeners(i).name());
  }
  createGeneratedApiTestServer(bootstrap_path, named_ports);
}

void BaseIntegrationTest::setUpstreamProtocol(FakeHttpConnection::Type protocol) {
  upstream_protocol_ = protocol;
  if (upstream_protocol_ == FakeHttpConnection::Type::HTTP2) {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
          RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1);
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          cluster->mutable_http2_protocol_options();
        });
  } else {
    RELEASE_ASSERT(protocol == FakeHttpConnection::Type::HTTP1);
  }
}

IntegrationTcpClientPtr BaseIntegrationTest::makeTcpConnection(uint32_t port) {
  return IntegrationTcpClientPtr{new IntegrationTcpClient(*dispatcher_, *mock_buffer_factory_, port,
                                                          version_, enable_half_close_)};
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

void BaseIntegrationTest::createGeneratedApiTestServer(const std::string& bootstrap_path,
                                                       const std::vector<std::string>& port_names) {
  test_server_ =
      IntegrationTestServer::create(bootstrap_path, version_, pre_worker_start_test_steps_);
  if (config_helper_.bootstrap().static_resources().listeners_size() > 0) {
    // Wait for listeners to be created before invoking registerTestServerPorts() below, as that
    // needs to know about the bound listener ports.
    test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
    registerTestServerPorts(port_names);
  }
}

void BaseIntegrationTest::createApiTestServer(const ApiFilesystemConfig& api_filesystem_config,
                                              const std::vector<std::string>& port_names) {
  const std::string eds_path = TestEnvironment::temporaryFileSubstitute(
      api_filesystem_config.eds_path_, port_map_, version_);
  const std::string cds_path = TestEnvironment::temporaryFileSubstitute(
      api_filesystem_config.cds_path_, {{"eds_json_path", eds_path}}, port_map_, version_);
  const std::string rds_path = TestEnvironment::temporaryFileSubstitute(
      api_filesystem_config.rds_path_, port_map_, version_);
  const std::string lds_path = TestEnvironment::temporaryFileSubstitute(
      api_filesystem_config.lds_path_, {{"rds_json_path", rds_path}}, port_map_, version_);
  createGeneratedApiTestServer(TestEnvironment::temporaryFileSubstitute(
                                   api_filesystem_config.bootstrap_path_,
                                   {{"cds_json_path", cds_path}, {"lds_json_path", lds_path}},
                                   port_map_, version_),
                               port_names);
}

void BaseIntegrationTest::createTestServer(const std::string& json_path,
                                           const std::vector<std::string>& port_names) {
  test_server_ = IntegrationTestServer::create(
      TestEnvironment::temporaryFileSubstitute(json_path, port_map_, version_), version_, nullptr);
  registerTestServerPorts(port_names);
}

void BaseIntegrationTest::sendRawHttpAndWaitForResponse(int port, const char* raw_http,
                                                        std::string* response,
                                                        bool disconnect_after_headers_complete) {
  Buffer::OwnedImpl buffer(raw_http);
  RawConnectionDriver connection(
      port, buffer,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response->append(TestUtility::bufferToString(data));
        if (disconnect_after_headers_complete && response->find("\r\n\r\n") != std::string::npos) {
          client.close(Network::ConnectionCloseType::NoFlush);
        }
      },
      version_);

  connection.run();
}

} // namespace Envoy
