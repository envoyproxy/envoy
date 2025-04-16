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
  NiceMock<MockConnectionSocket> socket_;

  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
  bool read_end_stream_{};
  bool write_end_stream_{};
  ConnectionCloseAction remote_close_action_ =
      ConnectionCloseAction{ConnectionEvent::RemoteClose, true};
  ConnectionCloseAction local_close_action_ =
      ConnectionCloseAction{ConnectionEvent::LocalClose, false, ConnectionCloseType::FlushWrite};
  ConnectionCloseAction local_close_socket_action_ =
      ConnectionCloseAction{ConnectionEvent::LocalClose, true};
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

TEST_F(NetworkFilterManagerTest, MultipleStopIterationAndDontCloseRead) {
  InSequence s;

  // Create multiple read filters to test multiple pending close behaviors
  MockReadFilter* read_filter_1(new MockReadFilter());
  MockReadFilter* read_filter_2(new MockReadFilter());
  MockReadFilter* read_filter_3(new MockReadFilter());

  FilterManagerImpl manager(connection_, socket_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter_1});
  manager.addReadFilter(ReadFilterSharedPtr{read_filter_2});
  manager.addReadFilter(ReadFilterSharedPtr{read_filter_3});

  // Initialize filters.
  EXPECT_CALL(*read_filter_1, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*read_filter_2, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*read_filter_3, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  // Set up read data.
  read_buffer_.add("hello world");

  // First filter disableClose() and StopIteration.
  EXPECT_CALL(*read_filter_1, onData(BufferStringEqual("hello world"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
        read_filter_1->callbacks_->disableClose(true);
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(*read_filter_2, onData(_, _)).Times(0);
  EXPECT_CALL(*read_filter_3, onData(_, _)).Times(0);
  manager.onRead();

  // Try to close the connection.
  EXPECT_CALL(connection_, closeConnection(_)).Times(0);
  manager.onConnectionClose(remote_close_action_);

  // Continue from first filter.
  EXPECT_CALL(*read_filter_2, onData(BufferStringEqual("hello world"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
        read_filter_2->callbacks_->disableClose(true);
        return FilterStatus::StopIteration;
      }));
  read_filter_1->callbacks_->continueReading();

  // One filter is still pending, so we shouldn't close yet.
  EXPECT_CALL(connection_, closeConnection(_)).Times(0);
  read_filter_1->callbacks_->disableClose(false);

  // After all filters continue closing, we should see the connection close.
  EXPECT_CALL(connection_, closeConnection(_));
  read_filter_2->callbacks_->disableClose(false);
}

TEST_F(NetworkFilterManagerTest, BothReadAndWriteFiltersHoldClose) {
  InSequence s;

  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter(new MockWriteFilter());
  MockFilter* filter(new MockFilter());

  FilterManagerImpl manager(connection_, socket_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter});
  manager.addFilter(FilterSharedPtr{filter});

  // Initialize filters
  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  // Make both read and write filters hold a pending close.
  read_buffer_.add("read_data");
  EXPECT_CALL(*read_filter, onData(BufferStringEqual("read_data"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
        read_filter->callbacks_->disableClose(true);
        return FilterStatus::StopIteration;
      }));
  manager.onRead();

  write_buffer_.add("write_data");
  EXPECT_CALL(*filter, onWrite(BufferStringEqual("write_data"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
        filter->write_callbacks_->disableClose(true);
        return FilterStatus::StopIteration;
      }));
  manager.onWrite();

  // Try to close the connection.
  EXPECT_CALL(connection_, closeConnection(_)).Times(0);
  manager.onConnectionClose(remote_close_action_);

  // After only read filter continues closing, we still shouldn't close.
  EXPECT_CALL(connection_, closeConnection(_)).Times(0);
  read_filter->callbacks_->disableClose(false);

  // After both filters continue closing, we should close.
  EXPECT_CALL(connection_, closeConnection(remote_close_action_));
  filter->write_callbacks_->disableClose(false);
}

TEST_F(NetworkFilterManagerTest, StopIterationAndDontCloseWithLocalClose) {
  InSequence s;

  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter(new MockWriteFilter());

  FilterManagerImpl manager(connection_, socket_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter});

  // Initialize filters.
  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  // Set up read data.
  read_buffer_.add("test data");
  EXPECT_CALL(*read_filter, onData(BufferStringEqual("test data"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
        read_filter->callbacks_->disableClose(true);
        return FilterStatus::StopIteration;
      }));
  manager.onRead();

  // Local close should be pending until filter allows it.
  EXPECT_CALL(connection_, closeConnection(_)).Times(0);
  manager.onConnectionClose(local_close_action_);

  // After the filter continues closing, we should see the connection close.
  EXPECT_CALL(connection_, closeConnection(local_close_action_));
  read_filter->callbacks_->disableClose(false);
}

TEST_F(NetworkFilterManagerTest, FinalizeCloseAfterFiltersComplete) {
  InSequence s;

  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter(new MockWriteFilter());

  FilterManagerImpl manager(connection_, socket_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter});

  // Initialize filters.
  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  // Set up read data with disableClose and StopIteration.
  read_buffer_.add("data");
  EXPECT_CALL(*read_filter, onData(BufferStringEqual("data"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
        read_filter->callbacks_->disableClose(true);
        return FilterStatus::StopIteration;
      }));
  manager.onRead();

  // Try remote close.
  manager.onConnectionClose(remote_close_action_);

  // Verify that close happens with proper event type after filter completes.
  EXPECT_CALL(connection_, closeConnection(remote_close_action_));
  read_filter->callbacks_->disableClose(false);
}

TEST_F(NetworkFilterManagerTest, LocalAndRemoteCloseRaceCondition) {
  InSequence s;

  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter(new MockWriteFilter());

  FilterManagerImpl manager(connection_, socket_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter});

  // Initialize filters with disableClose(true).
  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  read_buffer_.add("data");
  EXPECT_CALL(*read_filter, onData(BufferStringEqual("data"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
        read_filter->callbacks_->disableClose(true);
        return FilterStatus::StopIteration;
      }));
  manager.onRead();

  // Simulate both local and remote close happening.
  EXPECT_CALL(connection_, closeConnection(_)).Times(0);
  manager.onConnectionClose(local_close_action_);
  manager.onConnectionClose(remote_close_action_);

  // When filter continues, we should see connection closed with
  // the latest event type (RemoteClose).
  EXPECT_CALL(connection_, closeConnection(remote_close_action_));
  read_filter->callbacks_->disableClose(false);
}

TEST_F(NetworkFilterManagerTest, LocalCloseSocketAndRemoteCloseRace) {
  InSequence s;

  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter(new MockWriteFilter());

  FilterManagerImpl manager(connection_, socket_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter});

  // Initialize filters with disableClose(true).
  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  read_buffer_.add("data");
  EXPECT_CALL(*read_filter, onData(BufferStringEqual("data"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
        read_filter->callbacks_->disableClose(true);
        return FilterStatus::StopIteration;
      }));
  manager.onRead();

  // Simulate both local and remote close happening.
  EXPECT_CALL(connection_, closeConnection(_)).Times(0);
  manager.onConnectionClose(local_close_socket_action_);
  manager.onConnectionClose(remote_close_action_);

  // When filter continues, we should see it is still local close.
  EXPECT_CALL(connection_, closeConnection(local_close_socket_action_));
  read_filter->callbacks_->disableClose(false);
}

TEST_F(NetworkFilterManagerTest, MultipleFiltersWithDifferentStatusResponses) {
  InSequence s;

  // Create filters that will return different status codes.
  MockReadFilter* continue_filter(new MockReadFilter());
  MockReadFilter* stop_filter(new MockReadFilter());
  MockReadFilter* stop_dont_close_filter(new MockReadFilter());

  FilterManagerImpl manager(connection_, socket_);
  manager.addReadFilter(ReadFilterSharedPtr{continue_filter});
  manager.addReadFilter(ReadFilterSharedPtr{stop_filter});
  manager.addReadFilter(ReadFilterSharedPtr{stop_dont_close_filter});

  // Initialize filters
  EXPECT_CALL(*continue_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*stop_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*stop_dont_close_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  // Setup data and filter responses.
  read_buffer_.add("test");
  EXPECT_CALL(*continue_filter, onData(BufferStringEqual("test"), _))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*stop_filter, onData(BufferStringEqual("test"), _))
      .WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_CALL(*stop_dont_close_filter, onData(_, _)).Times(0);
  manager.onRead();

  // Continue from stop_filter.
  read_buffer_.add("more");
  EXPECT_CALL(*stop_dont_close_filter, onData(BufferStringEqual("testmore"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
        stop_dont_close_filter->callbacks_->disableClose(true);
        return FilterStatus::StopIteration;
      }));
  stop_filter->callbacks_->continueReading();

  // Now try to close again, should be held.
  EXPECT_CALL(connection_, closeConnection(_)).Times(0);
  manager.onConnectionClose(local_close_action_);

  // The socket close has a higher priority than the local_close_action_.
  EXPECT_CALL(connection_, closeConnection(local_close_action_));
  stop_dont_close_filter->callbacks_->disableClose(false);
}

TEST_F(NetworkFilterManagerTest, InjectReadDataWithStopIterationAndDontClose) {
  InSequence s;

  MockReadFilter* read_filter_1(new MockReadFilter());
  MockReadFilter* read_filter_2(new MockReadFilter());
  MockFilter* filter(new MockFilter());

  FilterManagerImpl manager(connection_, socket_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter_1});
  manager.addReadFilter(ReadFilterSharedPtr{read_filter_2});
  manager.addFilter(FilterSharedPtr{filter});

  // Initialize filters.
  EXPECT_CALL(*read_filter_1, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*read_filter_2, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  // First read filter disableClose(true).
  read_buffer_.add("original");
  EXPECT_CALL(*read_filter_1, onData(BufferStringEqual("original"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
        read_filter_1->callbacks_->disableClose(true);
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(*read_filter_2, onData(_, _)).Times(0);
  EXPECT_CALL(*filter, onData(_, _)).Times(0);
  manager.onRead();

  // Inject data through the stopped filter - should reach remaining filters.
  Buffer::OwnedImpl injected_data("injected");
  EXPECT_CALL(*read_filter_2, onData(BufferStringEqual("injected"), false))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onData(BufferStringEqual("injected"), false))
      .WillOnce(Return(FilterStatus::Continue));
  read_filter_1->callbacks_->injectReadDataToFilterChain(injected_data, false);

  // Inject more data.
  Buffer::OwnedImpl more_data("more");
  EXPECT_CALL(*read_filter_2, onData(BufferStringEqual("more"), true))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onData(BufferStringEqual("more"), true))
      .WillOnce(Return(FilterStatus::Continue));
  read_filter_1->callbacks_->injectReadDataToFilterChain(more_data, true);

  // Try to close the connection - should be held by filter.
  EXPECT_CALL(connection_, closeConnection(_)).Times(0);
  manager.onConnectionClose(remote_close_action_);

  // Continue reading should not affect the close status.
  EXPECT_CALL(*read_filter_2, onData(BufferStringEqual("original"), _))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onData(BufferStringEqual("original"), _))
      .WillOnce(Return(FilterStatus::Continue));
  read_filter_1->callbacks_->continueReading();

  // Connection should close after filter continues closing.
  EXPECT_CALL(connection_, closeConnection(remote_close_action_));
  read_filter_1->callbacks_->disableClose(false);
}

TEST_F(NetworkFilterManagerTest, InjectWriteDataWithStopIterationAndDontClose) {
  InSequence s;

  MockReadFilter* read_filter(new MockReadFilter());
  MockWriteFilter* write_filter_1(new MockWriteFilter());
  MockWriteFilter* write_filter_2(new MockWriteFilter());

  FilterManagerImpl manager(connection_, socket_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter_1});
  manager.addWriteFilter(WriteFilterSharedPtr{write_filter_2});

  // Initialize read filters.
  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  // Write filter disableClose(true).
  write_buffer_.add("original");
  EXPECT_CALL(*write_filter_2, onWrite(BufferStringEqual("original"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
        write_filter_2->write_callbacks_->disableClose(true);
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(*write_filter_1, onWrite(_, _)).Times(0);
  manager.onWrite();

  // Try to close the connection - should be held by filter.
  EXPECT_CALL(connection_, closeConnection(_)).Times(0);
  manager.onConnectionClose(local_close_action_);

  // Inject write data should bypass stopped filter and reach connection.
  Buffer::OwnedImpl injected_data("injected");
  EXPECT_CALL(*write_filter_1, onWrite(BufferStringEqual("injected"), false))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(connection_, rawWrite(BufferStringEqual("injected"), false));
  write_filter_2->write_callbacks_->injectWriteDataToFilterChain(injected_data, false);

  // Inject more data with end_stream.
  Buffer::OwnedImpl more_data("more");
  EXPECT_CALL(*write_filter_1, onWrite(BufferStringEqual("more"), true))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(connection_, rawWrite(BufferStringEqual("more"), true));
  write_filter_2->write_callbacks_->injectWriteDataToFilterChain(more_data, true);

  // Connection should close after filter continues closing.
  EXPECT_CALL(connection_, closeConnection(local_close_action_));
  write_filter_2->write_callbacks_->disableClose(false);
}

TEST_F(NetworkFilterManagerTest, ChainedInjectsWithMixedFilterStatus) {
  InSequence s;

  MockReadFilter* read_filter_1(new MockReadFilter());
  MockReadFilter* read_filter_2(new MockReadFilter());
  MockFilter* filter(new MockFilter());

  FilterManagerImpl manager(connection_, socket_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter_1});
  manager.addReadFilter(ReadFilterSharedPtr{read_filter_2});
  manager.addFilter(FilterSharedPtr{filter});

  // Initialize filters.
  EXPECT_CALL(*read_filter_1, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*read_filter_2, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  // First read filter stops the chain with disableClose(true).
  read_buffer_.add("start");
  EXPECT_CALL(*read_filter_1, onData(BufferStringEqual("start"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
        read_filter_1->callbacks_->disableClose(true);
        return FilterStatus::StopIteration;
      }));
  manager.onRead();

  // Inject data and continue the chain - second filter returns StopIteration.
  Buffer::OwnedImpl data1("inject1");
  EXPECT_CALL(*read_filter_2, onData(BufferStringEqual("inject1"), false))
      .WillOnce(Return(FilterStatus::StopIteration));
  read_filter_1->callbacks_->injectReadDataToFilterChain(data1, false);

  // Continue reading from second filter - filter returns Continue.
  Buffer::OwnedImpl data2("inject2");
  EXPECT_CALL(*filter, onData(BufferStringEqual("inject2"), true))
      .WillOnce(Return(FilterStatus::Continue));
  read_filter_2->callbacks_->injectReadDataToFilterChain(data2, true);

  // Try to close - should be held by the first filter only.
  EXPECT_CALL(connection_, closeConnection(_)).Times(0);
  manager.onConnectionClose(local_close_action_);

  // After filter continues, connection should close.
  EXPECT_CALL(connection_, closeConnection(local_close_action_));
  read_filter_1->callbacks_->disableClose(false);
}

TEST_F(NetworkFilterManagerTest, MultipleInjectDataCallsFromDifferentFilters) {
  InSequence s;

  MockReadFilter* read_filter_1(new MockReadFilter());
  MockReadFilter* read_filter_2(new MockReadFilter());
  MockFilter* filter(new MockFilter());

  FilterManagerImpl manager(connection_, socket_);
  manager.addReadFilter(ReadFilterSharedPtr{read_filter_1});
  manager.addReadFilter(ReadFilterSharedPtr{read_filter_2});
  manager.addFilter(FilterSharedPtr{filter});

  // Initialize filters.
  EXPECT_CALL(*read_filter_1, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*read_filter_2, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_EQ(manager.initializeReadFilters(), true);

  // First read filter StopIteration in sequence.
  read_buffer_.add("original");
  EXPECT_CALL(*read_filter_1, onData(BufferStringEqual("original"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
        read_filter_1->callbacks_->disableClose(true);
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(*read_filter_2, onData(_, _)).Times(0);
  manager.onRead();

  // Inject data from first filter.
  Buffer::OwnedImpl data1("data1");
  EXPECT_CALL(*read_filter_2, onData(BufferStringEqual("data1"), false))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
        read_filter_2->callbacks_->disableClose(true);
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(*filter, onData(_, _)).Times(0);
  read_filter_1->callbacks_->injectReadDataToFilterChain(data1, false);

  // Inject data from second filter.
  Buffer::OwnedImpl data2("data2");
  EXPECT_CALL(*filter, onData(BufferStringEqual("data2"), false))
      .WillOnce(Return(FilterStatus::Continue));
  read_filter_2->callbacks_->injectReadDataToFilterChain(data2, false);

  // Try to close the connection - should be held by both filters.
  EXPECT_CALL(connection_, closeConnection(_)).Times(0);
  manager.onConnectionClose(remote_close_action_);

  // First filter continues closing.
  EXPECT_CALL(connection_, closeConnection(_)).Times(0);
  read_filter_1->callbacks_->disableClose(false);

  // Second filter continues closing - now connection should close.
  EXPECT_CALL(connection_, closeConnection(remote_close_action_));
  read_filter_2->callbacks_->disableClose(false);
}

} // namespace
} // namespace Network
} // namespace Envoy
