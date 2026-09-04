#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/transport_socket_options_impl.h"

#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Optional;
using testing::ReturnRef;

namespace Envoy {
namespace Network {

TEST(RawBufferSocketFactory, RawBufferSocketFactory) {
  UpstreamTransportSocketFactoryPtr factory = Envoy::Network::Test::createRawBufferSocketFactory();
  EXPECT_FALSE(factory->implementsSecureTransport());
  std::vector<uint8_t> keys;
  factory->hashKey(keys, nullptr);
  EXPECT_EQ(keys.size(), 0);
  auto options = std::make_shared<TransportSocketOptionsImpl>("server");
  factory->hashKey(keys, options);
  EXPECT_GT(keys.size(), 0);
}

class RawBufferSocketReadLimitTest : public testing::Test {
protected:
  void initialize(std::optional<uint64_t> max_read_buffer_size) {
    socket_ = std::make_unique<RawBufferSocket>(max_read_buffer_size);
    ON_CALL(callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));
    socket_->setTransportSocketCallbacks(callbacks_);
  }

  NiceMock<MockIoHandle> io_handle_;
  NiceMock<MockTransportSocketCallbacks> callbacks_;
  std::unique_ptr<RawBufferSocket> socket_;
};

TEST_F(RawBufferSocketReadLimitTest, LimitsReadAndSchedulesRemainingData) {
  initialize(36);
  Buffer::OwnedImpl buffer;

  EXPECT_CALL(io_handle_, read(_, Optional(36)))
      .WillOnce(Invoke([](Buffer::Instance& data, std::optional<uint64_t>) {
        data.add(std::string(36, 'a'));
        return Api::IoCallUint64Result(36, Api::IoError::none());
      }));
  EXPECT_CALL(callbacks_, setTransportSocketIsReadable());

  const IoResult result = socket_->doRead(buffer);
  EXPECT_EQ(36, result.bytes_processed_);
  EXPECT_EQ(36, buffer.length());
}

TEST_F(RawBufferSocketReadLimitTest, AccountsForDataAlreadyInReadBuffer) {
  initialize(36);
  Buffer::OwnedImpl buffer(std::string(10, 'a'));

  EXPECT_CALL(io_handle_, read(_, Optional(26)))
      .WillOnce(Invoke([](Buffer::Instance& data, std::optional<uint64_t>) {
        data.add(std::string(26, 'b'));
        return Api::IoCallUint64Result(26, Api::IoError::none());
      }));
  EXPECT_CALL(callbacks_, setTransportSocketIsReadable());

  const IoResult result = socket_->doRead(buffer);
  EXPECT_EQ(26, result.bytes_processed_);
  EXPECT_EQ(36, buffer.length());
}

TEST_F(RawBufferSocketReadLimitTest, AccountsForDataConsumedBetweenReads) {
  initialize(36);
  Buffer::OwnedImpl buffer;

  EXPECT_CALL(io_handle_, read(_, Optional(36)))
      .WillOnce(Invoke([](Buffer::Instance& data, std::optional<uint64_t>) {
        data.add(std::string(20, 'a'));
        return Api::IoCallUint64Result(20, Api::IoError::none());
      }));
  EXPECT_CALL(callbacks_, shouldDrainReadBuffer())
      .WillOnce(testing::Return(true))
      .WillOnce(testing::Return(false));
  EXPECT_CALL(callbacks_, setTransportSocketIsReadable());

  const IoResult first_result = socket_->doRead(buffer);
  EXPECT_EQ(20, first_result.bytes_processed_);
  buffer.drain(buffer.length());

  EXPECT_CALL(io_handle_, read(_, Optional(16)))
      .WillOnce(Invoke([](Buffer::Instance& data, std::optional<uint64_t>) {
        data.add(std::string(16, 'b'));
        return Api::IoCallUint64Result(16, Api::IoError::none());
      }));
  EXPECT_CALL(callbacks_, setTransportSocketIsReadable());

  const IoResult second_result = socket_->doRead(buffer);
  EXPECT_EQ(16, second_result.bytes_processed_);
  EXPECT_EQ(16, buffer.length());
}

TEST_F(RawBufferSocketReadLimitTest, LeavesReadsUnlimitedByDefault) {
  initialize(std::nullopt);
  Buffer::OwnedImpl buffer;

  EXPECT_CALL(io_handle_, read(_, Eq(std::optional<uint64_t>{})))
      .WillOnce(Invoke([](Buffer::Instance& data, std::optional<uint64_t>) {
        data.add("plaintext");
        return Api::IoCallUint64Result(9, Api::IoError::none());
      }))
      .WillOnce(testing::Return(
          ByMove(Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEagainError()))));

  const IoResult result = socket_->doRead(buffer);
  EXPECT_EQ(9, result.bytes_processed_);
  EXPECT_EQ("plaintext", buffer.toString());
}

} // namespace Network
} // namespace Envoy
