#include "test/integration/integration.h"
#include "test/integration/utility.h"

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
      lookupPort("echo"),
      buffer, [&](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
        connection.close();
      }, version_);

  connection.run();
  EXPECT_EQ("hello", response);
}
} // Envoy
