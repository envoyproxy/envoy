#include <string>
#include <vector>

#include "envoy/extensions/filters/network/ratelimit/v3/rate_limit.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "common/buffer/buffer_impl.h"
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
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

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
namespace {

class NetworkFilterManagerTest : public testing::Test {
public:
  void SetUp() override {
    EXPECT_CALL(connection_, getReadBuffer).WillRepeatedly(Invoke([this]() {
      return StreamBuffer{read_buffer_, read_end_stream_};
    }));
    EXPECT_CALL(connection_, getWriteBuffer).WillRepeatedly(Invoke([this]() {
      return StreamBuffer{write_buffer_, write_end_stream_};
    }));
  }

  NiceMock<MockFilterManagerConnection> connection_;

  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
  bool read_end_stream_{};
  bool write_end_stream_{};
};

class LocalMockFilter : public MockFilter {
public:
  ~LocalMockFilter() override {
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

  FilterManagerImpl manager(connection_);
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

TEST_F(NetworkFilterManagerTest, ConnectionClosedBeforeRunningFilter) {
  InSequence s;

  Upstream::HostDescription* host_description(new NiceMock<Upstream::MockHostDescription>());
  MockReadFilter* read_filter(new MockReadFilter());
  MockFilter* filter(new LocalMockFilter());

  FilterManagerImpl manager(connection_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addFilter(FilterSharedPtr{filter});

  read_filter->callbacks_->upstreamHost(Upstream::HostDescriptionConstSharedPtr{host_description});
  EXPECT_EQ(read_filter->callbacks_->upstreamHost(), filter->callbacks_->upstreamHost());

  EXPECT_CALL(connection_, state()).WillOnce(Return(Connection::State::Closing));
  EXPECT_CALL(*read_filter, onNewConnection()).Times(0);
  EXPECT_CALL(*read_filter, onData(_, _)).Times(0);
  EXPECT_CALL(*filter, onNewConnection()).Times(0);
  EXPECT_CALL(*filter, onData(_, _)).Times(0);
  manager.onRead();

  EXPECT_CALL(connection_, state()).WillOnce(Return(Connection::State::Closed));
  EXPECT_CALL(*filter, onWrite(_, _)).Times(0);
  manager.onWrite();
}

TEST_F(NetworkFilterManagerTest, FilterReturnStopAndNoCallback) {
  InSequence s;

  Upstream::HostDescription* host_description(new NiceMock<Upstream::MockHostDescription>());
  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter(new MockWriteFilter());
  MockFilter* filter(new LocalMockFilter());

  FilterManagerImpl manager(connection_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter});
  manager.addFilter(FilterSharedPtr{filter});

  read_filter->callbacks_->upstreamHost(Upstream::HostDescriptionConstSharedPtr{host_description});
  EXPECT_EQ(read_filter->callbacks_->upstreamHost(), filter->callbacks_->upstreamHost());

  read_buffer_.add("hello");
  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*read_filter, onData(BufferStringEqual("hello"), _))
      .WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_CALL(*filter, onNewConnection()).Times(0);
  EXPECT_CALL(*filter, onData(_, _)).Times(0);
  manager.onRead();

  EXPECT_CALL(*filter, onWrite(_, _)).WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_CALL(*write_filter, onWrite(_, _)).Times(0);
  manager.onWrite();
}

TEST_F(NetworkFilterManagerTest, ReadFilterCloseConnectionAndReturnContinue) {
  InSequence s;

  Upstream::HostDescription* host_description(new NiceMock<Upstream::MockHostDescription>());
  MockReadFilter* read_filter(new MockReadFilter());
  MockFilter* filter(new LocalMockFilter());

  FilterManagerImpl manager(connection_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addFilter(FilterSharedPtr{filter});

  read_filter->callbacks_->upstreamHost(Upstream::HostDescriptionConstSharedPtr{host_description});
  EXPECT_EQ(read_filter->callbacks_->upstreamHost(), filter->callbacks_->upstreamHost());

  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  read_buffer_.add("hello");
  EXPECT_CALL(connection_, state()).WillOnce(Return(Connection::State::Open));
  EXPECT_CALL(*read_filter, onData(BufferStringEqual("hello"), _))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(connection_, state()).WillOnce(Return(Connection::State::Closing));
  EXPECT_CALL(*filter, onData(_, _)).Times(0);
  manager.onRead();

  EXPECT_CALL(connection_, state()).WillOnce(Return(Connection::State::Closed));
  EXPECT_CALL(*filter, onWrite(_, _)).Times(0);
  manager.onWrite();
}

TEST_F(NetworkFilterManagerTest, WriteFilterCloseConnectionAndReturnContinue) {
  InSequence s;

  Upstream::HostDescription* host_description(new NiceMock<Upstream::MockHostDescription>());
  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter(new MockWriteFilter());
  MockFilter* filter(new LocalMockFilter());

  FilterManagerImpl manager(connection_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter});
  manager.addFilter(FilterSharedPtr{filter});

  read_filter->callbacks_->upstreamHost(Upstream::HostDescriptionConstSharedPtr{host_description});
  EXPECT_EQ(read_filter->callbacks_->upstreamHost(), filter->callbacks_->upstreamHost());

  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  read_buffer_.add("hello");
  EXPECT_CALL(*read_filter, onData(BufferStringEqual("hello"), _))
      .WillOnce(Return(FilterStatus::StopIteration));
  manager.onRead();

  read_buffer_.add("world");
  EXPECT_CALL(*filter, onData(BufferStringEqual("helloworld"), _))
      .WillOnce(Return(FilterStatus::Continue));
  read_filter->callbacks_->continueReading();

  write_buffer_.add("foo");
  EXPECT_CALL(connection_, state()).WillOnce(Return(Connection::State::Open));
  EXPECT_CALL(*filter, onWrite(BufferStringEqual("foo"), _))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(connection_, state()).WillOnce(Return(Connection::State::Closing));
  EXPECT_CALL(*write_filter, onWrite(_, _)).Times(0);
  manager.onWrite();
}

TEST_F(NetworkFilterManagerTest, ReadCloseConnectionReturnStopAndCallback) {
  InSequence s;

  Upstream::HostDescription* host_description(new NiceMock<Upstream::MockHostDescription>());
  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter(new MockWriteFilter());
  MockFilter* filter(new LocalMockFilter());

  FilterManagerImpl manager(connection_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter});
  manager.addFilter(FilterSharedPtr{filter});

  read_filter->callbacks_->upstreamHost(Upstream::HostDescriptionConstSharedPtr{host_description});
  EXPECT_EQ(read_filter->callbacks_->upstreamHost(), filter->callbacks_->upstreamHost());

  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  read_buffer_.add("hello");
  EXPECT_CALL(*read_filter, onData(BufferStringEqual("hello"), _))
      .WillOnce(Return(FilterStatus::StopIteration));
  manager.onRead();

  EXPECT_CALL(connection_, state()).WillOnce(Return(Connection::State::Closing));
  EXPECT_CALL(*filter, onData(_, _)).Times(0);
  read_filter->callbacks_->continueReading();

  EXPECT_CALL(connection_, state()).WillOnce(Return(Connection::State::Closed));
  EXPECT_CALL(*filter, onWrite(_, _)).Times(0);
  manager.onWrite();
}

TEST_F(NetworkFilterManagerTest, WriteCloseConnectionReturnStopAndCallback) {
  InSequence s;

  Upstream::HostDescription* host_description(new NiceMock<Upstream::MockHostDescription>());
  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter(new MockWriteFilter());
  MockFilter* filter(new LocalMockFilter());

  FilterManagerImpl manager(connection_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter});
  manager.addFilter(FilterSharedPtr{filter});

  read_filter->callbacks_->upstreamHost(Upstream::HostDescriptionConstSharedPtr{host_description});
  EXPECT_EQ(read_filter->callbacks_->upstreamHost(), filter->callbacks_->upstreamHost());

  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  read_buffer_.add("hello");
  EXPECT_CALL(*read_filter, onData(BufferStringEqual("hello"), _))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onData(BufferStringEqual("hello"), _))
      .WillOnce(Return(FilterStatus::Continue));
  manager.onRead();

  write_buffer_.add("foo");
  EXPECT_CALL(connection_, state()).WillOnce(Return(Connection::State::Open));
  EXPECT_CALL(*filter, onWrite(BufferStringEqual("foo"), _))
      .WillOnce(Return(FilterStatus::StopIteration));
  manager.onWrite();

  EXPECT_CALL(connection_, state()).WillOnce(Return(Connection::State::Closed));
  EXPECT_CALL(*filter, onWrite(_, _)).Times(0);
  EXPECT_CALL(*write_filter, onWrite(_, _)).Times(0);
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

  FilterManagerImpl manager(connection_);
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
  NiceMock<MockClientConnection> upstream_connection;
  NiceMock<Tcp::ConnectionPool::MockInstance> conn_pool;
  FilterManagerImpl manager(connection_);

  std::string rl_yaml = R"EOF(
domain: foo
descriptors:
- entries:
  - key: hello
    value: world
stat_prefix: name
    )EOF";

  ON_CALL(factory_context.runtime_loader_.snapshot_,
          featureEnabled("ratelimit.tcp_filter_enabled", 100))
      .WillByDefault(Return(true));
  ON_CALL(factory_context.runtime_loader_.snapshot_,
          featureEnabled("ratelimit.tcp_filter_enforcing", 100))
      .WillByDefault(Return(true));

  envoy::extensions::filters::network::ratelimit::v3::RateLimit proto_config{};
  TestUtility::loadFromYaml(rl_yaml, proto_config);

  Extensions::NetworkFilters::RateLimitFilter::ConfigSharedPtr rl_config(
      new Extensions::NetworkFilters::RateLimitFilter::Config(proto_config, factory_context.scope_,
                                                              factory_context.runtime_loader_));
  Extensions::Filters::Common::RateLimit::MockClient* rl_client =
      new Extensions::Filters::Common::RateLimit::MockClient();
  manager.addReadFilter(std::make_shared<Extensions::NetworkFilters::RateLimitFilter::Filter>(
      rl_config, Extensions::Filters::Common::RateLimit::ClientPtr{rl_client}));

  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  tcp_proxy.set_stat_prefix("name");
  tcp_proxy.set_cluster("fake_cluster");
  TcpProxy::ConfigSharedPtr tcp_proxy_config(new TcpProxy::Config(tcp_proxy, factory_context));
  manager.addReadFilter(
      std::make_shared<TcpProxy::Filter>(tcp_proxy_config, factory_context.cluster_manager_));

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

  EXPECT_CALL(factory_context.cluster_manager_, tcpConnPoolForCluster("fake_cluster", _, _))
      .WillOnce(Return(&conn_pool));

  request_callbacks->complete(Extensions::Filters::Common::RateLimit::LimitStatus::OK, nullptr,
                              nullptr);

  conn_pool.poolReady(upstream_connection);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(upstream_connection, write(BufferEqual(&buffer), _));
  read_buffer_.add("hello");
  manager.onRead();

  connection_.raiseEvent(ConnectionEvent::RemoteClose);
}

TEST_F(NetworkFilterManagerTest, InjectReadDataToFilterChain) {
  InSequence s;

  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter(new MockWriteFilter());
  MockFilter* filter(new MockFilter());

  FilterManagerImpl manager(connection_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter});
  manager.addFilter(FilterSharedPtr{filter});

  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  EXPECT_CALL(*filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  read_filter->callbacks_->continueReading();

  read_buffer_.add("hello");
  read_end_stream_ = true;

  Buffer::OwnedImpl injected_buffer("greetings");
  EXPECT_CALL(*filter, onData(BufferStringEqual("greetings"), false))
      .WillOnce(Return(FilterStatus::Continue));
  read_filter->callbacks_->injectReadDataToFilterChain(injected_buffer, false);

  injected_buffer.add(" everyone");
  EXPECT_CALL(*filter, onData(BufferStringEqual("greetings everyone"), true))
      .WillOnce(Return(FilterStatus::Continue));
  read_filter->callbacks_->injectReadDataToFilterChain(injected_buffer, true);
}

TEST_F(NetworkFilterManagerTest, InjectWriteDataToFilterChain) {
  InSequence s;

  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter(new MockWriteFilter());
  MockFilter* filter(new MockFilter());

  FilterManagerImpl manager(connection_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter});
  manager.addFilter(FilterSharedPtr{filter});

  Buffer::OwnedImpl injected_buffer("greetings");
  EXPECT_CALL(*write_filter, onWrite(BufferStringEqual("greetings"), false))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(connection_, rawWrite(BufferStringEqual("greetings"), false));
  filter->write_callbacks_->injectWriteDataToFilterChain(injected_buffer, false);

  injected_buffer.add(" everyone!");
  EXPECT_CALL(*write_filter, onWrite(BufferStringEqual(" everyone!"), true))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(connection_, rawWrite(BufferStringEqual(" everyone!"), true));
  filter->write_callbacks_->injectWriteDataToFilterChain(injected_buffer, true);
}

} // namespace
} // namespace Network
} // namespace Envoy
