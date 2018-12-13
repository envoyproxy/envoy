#include <string>
#include <vector>

#include "common/buffer/buffer_impl.h"
#include "common/config/filter_json.h"
#include "common/network/filter_manager_impl.h"
#include "common/tcp_proxy/tcp_proxy.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/filters/network/ratelimit/ratelimit.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/filters/common/ratelimit/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::WithArgs;

namespace Envoy {
namespace Network {

class NetworkFilterManagerTest : public testing::Test, public BufferSource {
public:
  StreamBuffer getReadBuffer() override { return {read_buffer_, read_end_stream_}; }
  StreamBuffer getWriteBuffer() override { return {write_buffer_, write_end_stream_}; }

  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
  bool read_end_stream_{};
  bool write_end_stream_{};
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
  read_end_stream_ = false;
  EXPECT_CALL(*read_filter, onData(BufferStringEqual("hello"), false))
      .WillOnce(Return(FilterStatus::StopIteration));
  manager.onRead();

  read_buffer_.add("world");
  EXPECT_CALL(*filter, onData(BufferStringEqual("helloworld"), false))
      .WillOnce(Return(FilterStatus::Continue));
  read_filter->callbacks_->continueReading();

  write_buffer_.add("foo");
  write_end_stream_ = false;
  EXPECT_CALL(*filter, onWrite(BufferStringEqual("foo"), false))
      .WillOnce(Return(FilterStatus::StopIteration));
  manager.onWrite();

  write_buffer_.add("bar");
  EXPECT_CALL(*filter, onWrite(BufferStringEqual("foobar"), false))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*write_filter, onWrite(BufferStringEqual("foobar"), false))
      .WillOnce(Return(FilterStatus::Continue));
  manager.onWrite();
}

// AllWithInorderedWriteFilters verifies the same case of All, except that the write filters are
// placed in the same order to the configuration.
TEST_F(NetworkFilterManagerTest, AllWithInorderedWriteFilters) {
  InSequence s;

  Upstream::HostDescription* host_description(new NiceMock<Upstream::MockHostDescription>());
  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter(new MockWriteFilter());
  MockFilter* filter(new LocalMockFilter());

  NiceMock<MockConnection> connection;
  connection.setWriteFilterOrder(false);
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
  read_end_stream_ = false;
  EXPECT_CALL(*read_filter, onData(BufferStringEqual("hello"), false))
      .WillOnce(Return(FilterStatus::StopIteration));
  manager.onRead();

  read_buffer_.add("world");
  EXPECT_CALL(*filter, onData(BufferStringEqual("helloworld"), false))
      .WillOnce(Return(FilterStatus::Continue));
  read_filter->callbacks_->continueReading();

  write_buffer_.add("foo");
  write_end_stream_ = false;
  EXPECT_CALL(*write_filter, onWrite(BufferStringEqual("foo"), false))
      .WillOnce(Return(FilterStatus::StopIteration));
  manager.onWrite();

  write_buffer_.add("bar");
  EXPECT_CALL(*write_filter, onWrite(BufferStringEqual("foobar"), false))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onWrite(BufferStringEqual("foobar"), false))
      .WillOnce(Return(FilterStatus::Continue));
  manager.onWrite();
}

// Test that end_stream is delivered in the correct order with the data, even
// if FilterStatus::StopIteration occurs.
TEST_F(NetworkFilterManagerTest, EndStream) {
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

  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  read_buffer_.add("hello");
  read_end_stream_ = true;
  EXPECT_CALL(*read_filter, onData(BufferStringEqual("hello"), true))
      .WillOnce(Return(FilterStatus::StopIteration));
  manager.onRead();

  read_buffer_.add("world");
  EXPECT_CALL(*filter, onData(BufferStringEqual("helloworld"), true))
      .WillOnce(Return(FilterStatus::Continue));
  read_filter->callbacks_->continueReading();

  write_buffer_.add("foo");
  write_end_stream_ = true;
  EXPECT_CALL(*filter, onWrite(BufferStringEqual("foo"), true))
      .WillOnce(Return(FilterStatus::StopIteration));
  manager.onWrite();

  write_buffer_.add("bar");
  EXPECT_CALL(*filter, onWrite(BufferStringEqual("foobar"), true))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*write_filter, onWrite(BufferStringEqual("foobar"), true))
      .WillOnce(Return(FilterStatus::Continue));
  manager.onWrite();
}

// This is a very important flow so make sure it works correctly in aggregate.
TEST_F(NetworkFilterManagerTest, RateLimitAndTcpProxy) {
  InSequence s;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  NiceMock<MockConnection> connection;
  NiceMock<MockClientConnection> upstream_connection;
  NiceMock<Tcp::ConnectionPool::MockInstance> conn_pool;
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

  ON_CALL(factory_context.runtime_loader_.snapshot_,
          featureEnabled("ratelimit.tcp_filter_enabled", 100))
      .WillByDefault(Return(true));
  ON_CALL(factory_context.runtime_loader_.snapshot_,
          featureEnabled("ratelimit.tcp_filter_enforcing", 100))
      .WillByDefault(Return(true));

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(rl_json);
  envoy::config::filter::network::rate_limit::v2::RateLimit proto_config{};
  Config::FilterJson::translateTcpRateLimitFilter(*json_config, proto_config);

  Extensions::NetworkFilters::RateLimitFilter::ConfigSharedPtr rl_config(
      new Extensions::NetworkFilters::RateLimitFilter::Config(proto_config, factory_context.scope_,
                                                              factory_context.runtime_loader_));
  Extensions::Filters::Common::RateLimit::MockClient* rl_client =
      new Extensions::Filters::Common::RateLimit::MockClient();
  manager.addReadFilter(std::make_shared<Extensions::NetworkFilters::RateLimitFilter::Filter>(
      rl_config, Extensions::Filters::Common::RateLimit::ClientPtr{rl_client}));

  envoy::config::filter::network::tcp_proxy::v2::TcpProxy tcp_proxy;
  tcp_proxy.set_stat_prefix("name");
  tcp_proxy.set_cluster("fake_cluster");
  TcpProxy::ConfigSharedPtr tcp_proxy_config(new TcpProxy::Config(tcp_proxy, factory_context));
  manager.addReadFilter(
      std::make_shared<TcpProxy::Filter>(tcp_proxy_config, factory_context.cluster_manager_,
                                         factory_context.dispatcher().timeSystem()));

  Extensions::Filters::Common::RateLimit::RequestCallbacks* request_callbacks{};
  EXPECT_CALL(*rl_client, limit(_, "foo",
                                testing::ContainerEq(
                                    std::vector<RateLimit::Descriptor>{{{{"hello", "world"}}}}),
                                testing::A<Tracing::Span&>()))
      .WillOnce(WithArgs<0>(
          Invoke([&](Extensions::Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks = &callbacks;
          })));

  EXPECT_EQ(manager.initializeReadFilters(), true);

  EXPECT_CALL(factory_context.cluster_manager_, tcpConnPoolForCluster("fake_cluster", _, _, _))
      .WillOnce(Return(&conn_pool));

  request_callbacks->complete(Extensions::Filters::Common::RateLimit::LimitStatus::OK, nullptr);

  conn_pool.poolReady(upstream_connection);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(upstream_connection, write(BufferEqual(&buffer), _));
  read_buffer_.add("hello");
  manager.onRead();

  connection.raiseEvent(ConnectionEvent::RemoteClose);
}

} // namespace Network
} // namespace Envoy
