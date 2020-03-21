#include <pthread.h>

#include "test/integration/fake_upstream.h" 
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

	const std::string CONFIG = R"EOF(
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    name: cluster_0
    type: STATIC
    load_assignment:
      cluster_name: cluster_0
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
  listeners:
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      - filters:
          - name: postgresql
            typed_config:
              "@type": type.googleapis.com/envoy.config.filter.network.postgresql_proxy.v2alpha.PostgreSQLProxy
              stat_prefix: postgresql_stats
          - name: tcp
            typed_config:
              "@type": type.googleapis.com/envoy.config.filter.network.tcp_proxy.v2.TcpProxy
              stat_prefix: tcp_stats
              cluster: cluster_0
)EOF";
class PostgreSQLIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>, 
	public BaseIntegrationTest {

#if 0
		std::string postgres_config() {
			return fmt::format(TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
							"test/extensions/filters/network/postgresql_proxy/postgresql_test_config.yaml")),
					Network::Test::getLoopbackAddressString(GetParam()),
					Network::Test::getLoopbackAddressString(GetParam()), 
					Network::Test::getAnyAddressString(GetParam()));
		}
#endif
	public:
  PostgreSQLIntegrationTest() : BaseIntegrationTest(GetParam(), CONFIG /*postgres_config()*/) {};

  void SetUp() override { BaseIntegrationTest::initialize(); }

  void TearDown() override {
//	  test_server_.reset();
	  fake_upstreams_.clear();
  }
};
INSTANTIATE_TEST_SUITE_P(IpVersions, PostgreSQLIntegrationTest,
		testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(PostgreSQLIntegrationTest, Login) {
	std::string str;
	std::string recv;

	IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0")); 
	FakeRawConnectionPtr fake_upstream_connection;
	ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection)); 

#if 0
	// startup message
	std::string startup = "start";
	ASSERT_TRUE(fake_upstream_connection->write(startup));

	str.append(startup);
	tcp_client->waitForData(str, true);
#endif

	EXPECT_TRUE(fake_upstream_connection->close());
	tcp_client->close();
	ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

	test_server_->waitForCounterEq("postgresql.postgresql_stats.sessions", 0);
}

#if 0
TEST_P(PostgreSQLIntegrationTest, TestNumber2) {
	std::string str;
	std::string recv;

	IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0")); 
	FakeRawConnectionPtr fake_upstream_connection;
	ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection)); 

#if 0
	// startup message
	std::string startup = "start";
	ASSERT_TRUE(fake_upstream_connection->write(startup));

	str.append(startup);
	tcp_client->waitForData(str, true);
#endif

	tcp_client->close();
	ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

	test_server_->waitForCounterEq("postgresql.postgresql_stats.sessions", 0);
}
#endif

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
