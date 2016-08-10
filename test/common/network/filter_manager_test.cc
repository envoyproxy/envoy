#include "common/buffer/buffer_impl.h"
#include "common/network/filter_manager.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/host.h"

using testing::InSequence;
using testing::Return;

namespace Network {

class NetworkFilterManagerTest : public testing::Test, public BufferSource {
public:
  Buffer::Instance& getReadBuffer() override { return read_buffer_; }
  Buffer::Instance& getWriteBuffer() override { return write_buffer_; }

  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
};

TEST_F(NetworkFilterManagerTest, All) {
  InSequence s;
  std::shared_ptr<MockReadFilter> read_filter(new MockReadFilter());
  std::shared_ptr<MockWriteFilter> write_filter(new MockWriteFilter());
  std::shared_ptr<MockFilter> filter(new MockFilter());

  MockConnection connection;
  FilterManager manager(connection, *this);
  manager.addReadFilter(read_filter);
  manager.addWriteFilter(write_filter);
  manager.addFilter(filter);

  Upstream::HostDescriptionPtr host_description(new Upstream::MockHostDescription());
  read_filter->callbacks_->upstreamHost(host_description);
  EXPECT_EQ(read_filter->callbacks_->upstreamHost(), filter->callbacks_->upstreamHost());

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

} // Network
