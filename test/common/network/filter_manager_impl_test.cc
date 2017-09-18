#include <string>
#include <vector>

#include "common/buffer/buffer_impl.h"
#include "common/filter/ratelimit.h"
#include "common/filter/tcp_proxy.h"
#include "common/network/filter_manager_impl.h"
#include "common/stats/stats_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::WithArgs;
using testing::_;

namespace Envoy {
namespace Network {

class NetworkFilterManagerTest : public testing::Test, public BufferSource {
public:
  Buffer::Instance& getReadBuffer() override { return read_buffer_; }
  Buffer::Instance& getWriteBuffer() override { return write_buffer_; }

  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
};

class LocalMockFilter : public MockFilter {
public:
  ~LocalMockFilter() {
    // Make sure the upstream host is still valid in the filter destructor.
    callbacks_->upstreamHost()->address();
  }
};

TEST_F(NetworkFilterManagerTest, All) {
  InSequence s;

  Upstream::HostDescription* host_description(new NiceMock<Upstream::MockHostDescription>());
  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter(new MockWriteFilter());
  MockFilter* filter(new LocalMockFilter());

  NiceMock<MockConnection> connection;
  FilterManagerImpl manager(connection, *this);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter});
  manager.addFilter(FilterSharedPtr{filter});

  read_filter->callbacks_->upstreamHost(Upstream::HostDescriptionConstSharedPtr{host_description});
  EXPECT_EQ(read_filter->callbacks_->upstreamHost(), filter->callbacks_->upstreamHost());

  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  EXPECT_CALL(*filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  read_filter->callbacks_->continueReading();

  read_buffer_.add("hello");
  EXPECT_CALL(*read_filter, onData(BufferStringEqual("hello")))
      .WillOnce(Return(FilterStatus::StopIteration));
  manager.onRead();

  read_buffer_.add("world");
  EXPECT_CALL(*filter, onData(BufferStringEqual("helloworld")))
      .WillOnce(Return(FilterStatus::Continue));
  read_filter->callbacks_->continueReading();

  write_buffer_.add("foo");
  EXPECT_CALL(*write_filter, onWrite(BufferStringEqual("foo")))
      .WillOnce(Return(FilterStatus::StopIteration));
  manager.onWrite();

  write_buffer_.add("bar");
  EXPECT_CALL(*write_filter, onWrite(BufferStringEqual("foobar")))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onWrite(BufferStringEqual("foobar")))
      .WillOnce(Return(FilterStatus::Continue));
  manager.onWrite();
}

// This is a very important flow so make sure it works correctly in aggregate.
TEST_F(NetworkFilterManagerTest, RateLimitAndTcpProxy) {
  InSequence s;
  Stats::IsolatedStoreImpl stats_store;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<MockConnection> connection;
  FilterManagerImpl manager(connection, *this);

  std::string rl_json = R"EOF(
    {
      "domain": "foo",
      "descriptors": [
         [{"key": "hello", "value": "world"}]
       ],
       "stat_prefix": "name"
    }
    )EOF";

  ON_CALL(runtime.snapshot_, featureEnabled("ratelimit.tcp_filter_enabled", 100))
      .WillByDefault(Return(true));
  ON_CALL(runtime.snapshot_, featureEnabled("ratelimit.tcp_filter_enforcing", 100))
      .WillByDefault(Return(true));

  Json::ObjectSharedPtr rl_config_loader = Json::Factory::loadFromString(rl_json);

  RateLimit::TcpFilter::ConfigSharedPtr rl_config(
      new RateLimit::TcpFilter::Config(*rl_config_loader, stats_store, runtime));
  RateLimit::MockClient* rl_client = new RateLimit::MockClient();
  manager.addReadFilter(ReadFilterSharedPtr{
      new RateLimit::TcpFilter::Instance(rl_config, RateLimit::ClientPtr{rl_client})});

  std::string tcp_proxy_json = R"EOF(
    {
      "stat_prefix": "name",
      "route_config": {
        "routes": [
          {
            "cluster": "fake_cluster"
          }
        ]
      }
    }
    )EOF";

  Json::ObjectSharedPtr tcp_proxy_config_loader = Json::Factory::loadFromString(tcp_proxy_json);
  Envoy::Filter::TcpProxyConfigSharedPtr tcp_proxy_config(
      new Envoy::Filter::TcpProxyConfig(*tcp_proxy_config_loader, cm, stats_store));
  manager.addReadFilter(ReadFilterSharedPtr{new Envoy::Filter::TcpProxy(tcp_proxy_config, cm)});

  RateLimit::RequestCallbacks* request_callbacks{};
  EXPECT_CALL(*rl_client, limit(_, "foo",
                                testing::ContainerEq(
                                    std::vector<RateLimit::Descriptor>{{{{"hello", "world"}}}}),
                                "", testing::A<Tracing::Span&>()))
      .WillOnce(WithArgs<0>(Invoke([&](RateLimit::RequestCallbacks& callbacks) -> void {
        request_callbacks = &callbacks;
      })));

  EXPECT_EQ(manager.initializeReadFilters(), true);

  NiceMock<Network::MockClientConnection>* upstream_connection =
      new NiceMock<Network::MockClientConnection>();
  Upstream::MockHost::MockCreateConnectionData conn_info;
  conn_info.connection_ = upstream_connection;
  conn_info.host_description_ =
      Upstream::makeTestHost(cm.thread_local_cluster_.cluster_.info_, "tcp://127.0.0.1:80");
  EXPECT_CALL(cm, tcpConnForCluster_("fake_cluster", _)).WillOnce(Return(conn_info));

  request_callbacks->complete(RateLimit::LimitStatus::OK);

  upstream_connection->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connection, write(BufferEqual(&buffer)));
  read_buffer_.add("hello");
  manager.onRead();
}

} // namespace Network
} // namespace Envoy
