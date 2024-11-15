#include <string>
#include <vector>

#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/filter_manager_impl.h"
#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/common/upstream/upstream_impl.h"

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
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

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
  NiceMock<MockListenSocket> socket_;

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

  FilterManagerImpl manager(connection_, socket_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter});
  manager.addFilter(FilterSharedPtr{filter});

  read_filter->callbacks_->upstreamHost(Upstream::HostDescriptionConstSharedPtr{host_description});
  EXPECT_EQ(read_filter->callbacks_->upstreamHost(), filter->callbacks_->upstreamHost());
  EXPECT_EQ(&read_filter->callbacks_->socket(), &socket_);
  EXPECT_EQ(&write_filter->write_callbacks_->socket(), &socket_);

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

  FilterManagerImpl manager(connection_, socket_);
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

  FilterManagerImpl manager(connection_, socket_);
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

  FilterManagerImpl manager(connection_, socket_);
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

  FilterManagerImpl manager(connection_, socket_);
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

  FilterManagerImpl manager(connection_, socket_);
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

  FilterManagerImpl manager(connection_, socket_);
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

  FilterManagerImpl manager(connection_, socket_);
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

TEST_F(NetworkFilterManagerTest, InjectReadDataToFilterChain) {
  InSequence s;

  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter(new MockWriteFilter());
  MockFilter* filter(new MockFilter());

  FilterManagerImpl manager(connection_, socket_);
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

  FilterManagerImpl manager(connection_, socket_);
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

TEST_F(NetworkFilterManagerTest, StartUpstreamSecureTransport) {
  InSequence s;

  MockReadFilter* read_filter_1(new MockReadFilter());
  MockReadFilter* read_filter_2(new MockReadFilter());
  MockFilter* filter(new MockFilter());

  FilterManagerImpl manager(connection_, socket_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter_1});
  manager.addReadFilter(ReadFilterSharedPtr{read_filter_2});
  manager.addFilter(FilterSharedPtr{filter});

  // Verify that filter manager calls each filter's 'startUpstreamsecureTransport' method.
  // when one filter calls startUpstreamSecureTransport.
  EXPECT_CALL(*read_filter_1, startUpstreamSecureTransport);
  EXPECT_CALL(*read_filter_2, startUpstreamSecureTransport);
  filter->callbacks_->startUpstreamSecureTransport();
}

} // namespace
} // namespace Network
} // namespace Envoy
