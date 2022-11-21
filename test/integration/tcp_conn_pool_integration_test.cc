#include <list>

#include "envoy/server/filter_config.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/server/utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace {

std::string tcp_conn_pool_config;

// Trivial Filter that obtains connections from a TCP connection pool each time onData is called
// and sends the data to the resulting upstream. The upstream's response is sent directly to
// the downstream.
class TestFilter : public Network::ReadFilter {
public:
  TestFilter(Upstream::ClusterManager& cluster_manager) : cluster_manager_(cluster_manager) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
    UNREFERENCED_PARAMETER(end_stream);

    absl::optional<Upstream::TcpPoolData> pool_data =
        cluster_manager_.getThreadLocalCluster("cluster_0")
            ->tcpConnPool(Upstream::ResourcePriority::Default, nullptr);
    ASSERT(pool_data.has_value());

    requests_.emplace_back(*this, data);
    pool_data.value().newConnection(requests_.back());

    ASSERT(data.length() == 0);
    return Network::FilterStatus::StopIteration;
  }
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  class Request : public Tcp::ConnectionPool::Callbacks,
                  public Tcp::ConnectionPool::UpstreamCallbacks {
  public:
    Request(TestFilter& parent, Buffer::Instance& data) : parent_(parent) { data_.move(data); }

    // Tcp::ConnectionPool::Callbacks
    void onPoolFailure(ConnectionPool::PoolFailureReason, absl::string_view,
                       Upstream::HostDescriptionConstSharedPtr) override {
      ASSERT(false);
    }

    void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                     Upstream::HostDescriptionConstSharedPtr) override {
      upstream_ = std::move(conn);

      upstream_->addUpstreamCallbacks(*this);
      upstream_->connection().write(data_, false);
    }

    // Tcp::ConnectionPool::UpstreamCallbacks
    void onUpstreamData(Buffer::Instance& data, bool end_stream) override {
      UNREFERENCED_PARAMETER(end_stream);

      Network::Connection& downstream = parent_.read_callbacks_->connection();
      downstream.write(data, false);

      upstream_.reset();
    }
    void onEvent(Network::ConnectionEvent) override {}
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    TestFilter& parent_;
    Buffer::OwnedImpl data_;
    Tcp::ConnectionPool::ConnectionDataPtr upstream_;
  };

  Upstream::ClusterManager& cluster_manager_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  std::list<Request> requests_;
};

class TestFilterConfigFactory : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext& context) override {
    return [&context](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<TestFilter>(context.clusterManager()));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { CONSTRUCT_ON_FIRST_USE(std::string, "envoy.test.router"); }
  bool isTerminalFilterByProto(const Protobuf::Message&,
                               Server::Configuration::ServerFactoryContext&) override {
    return true;
  }
};

} // namespace

class TcpConnPoolIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public BaseIntegrationTest {
public:
  TcpConnPoolIntegrationTest()
      : BaseIntegrationTest(GetParam(), tcp_conn_pool_config), filter_resolver_(config_factory_) {}

  // Called once by the gtest framework before any tests are run.
  static void SetUpTestSuite() { // NOLINT(readability-identifier-naming)
    tcp_conn_pool_config = absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      - filters:
        - name: envoy.test.router
          typed_config:
      )EOF");
  }

private:
  TestFilterConfigFactory config_factory_;
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory> filter_resolver_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, TcpConnPoolIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(TcpConnPoolIntegrationTest, SingleRequest) {
  initialize();

  std::string request("request");
  std::string response("response");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(request));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(request.size()));
  ASSERT_TRUE(fake_upstream_connection->write(response));

  tcp_client->waitForData(response);
  tcp_client->close();
}

TEST_P(TcpConnPoolIntegrationTest, MultipleRequests) {
  initialize();

  std::string request1("request1");
  std::string request2("request2");
  std::string response1("response1");
  std::string response2("response2");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));

  // send request 1
  ASSERT_TRUE(tcp_client->write(request1));
  FakeRawConnectionPtr fake_upstream_connection1;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection1));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection1->waitForData(request1.size(), &data));
  EXPECT_EQ(request1, data);

  // send request 2
  ASSERT_TRUE(tcp_client->write(request2));
  FakeRawConnectionPtr fake_upstream_connection2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection2));
  ASSERT_TRUE(fake_upstream_connection2->waitForData(request2.size(), &data));
  EXPECT_EQ(request2, data);

  // send response 2
  ASSERT_TRUE(fake_upstream_connection2->write(response2));
  tcp_client->waitForData(response2);

  // send response 1
  ASSERT_TRUE(fake_upstream_connection1->write(response1));
  tcp_client->waitForData(response1, false);

  tcp_client->close();
}

TEST_P(TcpConnPoolIntegrationTest, PoolCleanupEnabled) {
  // The test first does two requests concurrently, resulting in a single pool (it is never idle
  // between the first two), followed by going idle, then another request, which should create a
  // second pool, which is why the log message is expected 2 times. If the initial pool was not
  // cleaned up, only 1 pool would be created.
  EXPECT_LOG_CONTAINS_N_TIMES("debug", "Allocating TCP conn pool", 2, {
    initialize();

    std::string request1("request1");
    std::string request2("request2");
    std::string request3("request3");
    std::string response1("response1");
    std::string response2("response2");
    std::string response3("response3");

    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));

    // Send request 1.
    ASSERT_TRUE(tcp_client->write(request1));
    FakeRawConnectionPtr fake_upstream_connection1;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection1));
    std::string data;
    ASSERT_TRUE(fake_upstream_connection1->waitForData(request1.size(), &data));
    EXPECT_EQ(request1, data);

    // Send request 2.
    ASSERT_TRUE(tcp_client->write(request2));
    FakeRawConnectionPtr fake_upstream_connection2;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection2));
    ASSERT_TRUE(fake_upstream_connection2->waitForData(request2.size(), &data));
    EXPECT_EQ(request2, data);

    test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", 2);

    // Send response 2.
    ASSERT_TRUE(fake_upstream_connection2->write(response2));
    ASSERT_TRUE(fake_upstream_connection2->close());
    tcp_client->waitForData(response2);

    // Send response 1.
    ASSERT_TRUE(fake_upstream_connection1->write(response1));
    ASSERT_TRUE(fake_upstream_connection1->close());
    tcp_client->waitForData(response1, false);
    test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", 0);

    // After both requests were completed, the pool went idle and was cleaned up. Request 3 causes a
    // new pool to be created. Seeing a new pool created is a proxy for directly observing that an
    // old pool was cleaned up.
    //
    // TODO(ggreenway): if pool circuit breakers are implemented for tcp pools, verify cleanup by
    // looking at stats such as `cluster.cluster_0.circuit_breakers.default.cx_pool_open`.

    // Send request 3.
    ASSERT_TRUE(tcp_client->write(request3));
    FakeRawConnectionPtr fake_upstream_connection3;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection3));
    test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", 1);
    ASSERT_TRUE(fake_upstream_connection3->waitForData(request3.size(), &data));
    EXPECT_EQ(request3, data);

    ASSERT_TRUE(fake_upstream_connection3->write(response3));
    ASSERT_TRUE(fake_upstream_connection3->close());
    tcp_client->waitForData(response3, false);

    test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", 0);

    tcp_client->close();
  });
}

TEST_P(TcpConnPoolIntegrationTest, ShutdownWithOpenConnections) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));

  // Establish downstream and upstream connections.
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  test_server_.reset();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForDisconnect();

  // Success criteria is that no ASSERTs fire and there are no leaks.
}

} // namespace Envoy
