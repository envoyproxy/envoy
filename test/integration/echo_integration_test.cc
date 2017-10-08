#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/server/utility.h"

namespace Envoy {
class EchoIntegrationTest : public BaseIntegrationTest,
                            public testing::TestWithParam<Network::Address::IpVersion> {
public:
  EchoIntegrationTest() : BaseIntegrationTest(GetParam()) {}
  /**
   * Initializer for an individual test.
   */
  void SetUp() override {
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream_1", fake_upstreams_.back()->localAddress()->ip()->port());
    createTestServer("test/config/integration/echo_server.json", {"echo"});
  }

  /**
   *  Destructor for an individual test.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, EchoIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(EchoIntegrationTest, Hello) {
  Buffer::OwnedImpl buffer("hello");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("echo"), buffer,
      [&](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
        connection.close();
      },
      version_);

  connection.run();
  EXPECT_EQ("hello", response);
}

TEST_P(EchoIntegrationTest, AddRemoveListener) {
  const std::string json = TestEnvironment::substitute(R"EOF(
  {
    "name": "new_listener",
    "address": "tcp://{{ ip_loopback_address }}:0",
    "filters": [
      { "name": "echo", "config": {} }
    ]
  }
  )EOF",
                                                       GetParam());

  // Add the listener.
  ConditionalInitializer listener_added_by_worker;
  ConditionalInitializer listener_added_by_manager;
  test_server_->setOnWorkerListenerAddedCb(
      [&listener_added_by_worker]() -> void { listener_added_by_worker.setReady(); });
  test_server_->server().dispatcher().post([this, json, &listener_added_by_manager]() -> void {
    EXPECT_TRUE(test_server_->server().listenerManager().addOrUpdateListener(
        Server::parseListenerFromJson(json)));
    listener_added_by_manager.setReady();
  });
  listener_added_by_worker.waitReady();
  listener_added_by_manager.waitReady();

  EXPECT_EQ(2UL, test_server_->server().listenerManager().listeners().size());
  uint32_t new_listener_port = test_server_->server()
                                   .listenerManager()
                                   .listeners()[1]
                                   .get()
                                   .socket()
                                   .localAddress()
                                   ->ip()
                                   ->port();

  Buffer::OwnedImpl buffer("hello");
  std::string response;
  RawConnectionDriver connection(
      new_listener_port, buffer,
      [&](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
        connection.close();
      },
      version_);
  connection.run();
  EXPECT_EQ("hello", response);

  // Remove the listener.
  ConditionalInitializer listener_removed;
  test_server_->setOnWorkerListenerRemovedCb(
      [&listener_removed]() -> void { listener_removed.setReady(); });
  test_server_->server().dispatcher().post([this]() -> void {
    EXPECT_TRUE(test_server_->server().listenerManager().removeListener("new_listener"));
  });
  listener_removed.waitReady();

  // Now connect. This should fail.
  RawConnectionDriver connection2(
      new_listener_port, buffer,
      [&](Network::ClientConnection&, const Buffer::Instance&) -> void { FAIL(); }, version_);
  connection2.run();
}

} // namespace Envoy
