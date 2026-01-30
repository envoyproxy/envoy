#include <chrono>
#include <cstddef>
#include <memory>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/quic/envoy_quic_packet_writer.h"
#include "source/common/quic/envoy_quic_utils.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/extensions/quic_packet_writer/platform/platform_packet_writer_factory.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Quic {

class QuicPlatformPacketWriterFactoryTest : public ::testing::Test {
public:
  QuicPlatformPacketWriterFactoryTest() : factory_(dispatcher_) {
    // Prepare the mock file event and expectation before creating/initializing the socket
    file_event_ = new Event::MockFileEvent();
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).WillOnce(Return(file_event_));

    Network::Address::InstanceConstSharedPtr peer_address =
        quicAddressToEnvoyAddressInstance(peer_addr_);
    Network::Address::InstanceConstSharedPtr local_address =
        quicAddressToEnvoyAddressInstance(quic::QuicSocketAddress(self_ip_, 0));
    QuicClientPacketWriterFactory::CreationResult result = factory_.createSocketAndQuicPacketWriter(
        peer_address, /*network=*/123, local_address, nullptr);
    packet_writer_ = std::move(result.writer_);
    client_socket_ = std::move(result.socket_);
    EXPECT_TRUE(client_socket_->ioHandle().isOpen());

    client_socket_->ioHandle().initializeFileEvent(
        dispatcher_, [&](uint32_t) { return absl::OkStatus(); }, Event::FileTriggerType::Edge,
        Event::FileReadyType::Write);
  }

  void SetUp() override {
    injector_ =
        std::make_unique<TestThreadsafeSingletonInjector<Api::OsSysCallsImpl>>(&os_sys_calls_);
  }

  void TearDown() override { injector_.reset(); }

  void writePacketAndVerifyResult(quic::WriteStatus expected_status,
                                  absl::optional<int> expected_error_code = std::nullopt) {
    auto result = packet_writer_->WritePacket(packet_data_.data(), packet_data_.length(), self_ip_,
                                              peer_addr_, nullptr, {});
    EXPECT_EQ(expected_status, result.status);
    // Infer whether writer should be blocked from the status enum.
    EXPECT_EQ(expected_status == quic::WRITE_STATUS_BLOCKED, packet_writer_->IsWriteBlocked());
    if (expected_error_code.has_value()) {
      EXPECT_EQ(expected_error_code.value(), result.error_code);
    }
  }

protected:
  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  std::unique_ptr<TestThreadsafeSingletonInjector<Api::OsSysCallsImpl>> injector_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockFileEvent* file_event_{nullptr};
  QuicPlatformPacketWriterFactory factory_;
  std::unique_ptr<EnvoyQuicPacketWriter> packet_writer_;
  Network::ConnectionSocketPtr client_socket_;
  std::string packet_data_{"Hello World!"};
  quic::QuicIpAddress self_ip_{quic::QuicIpAddress::Loopback6()};
  quic::QuicSocketAddress peer_addr_{quic::QuicIpAddress::Any6(), 443};
};

// Tests successful write.
TEST_F(QuicPlatformPacketWriterFactoryTest, WritePacketSuccess) {
  EXPECT_CALL(os_sys_calls_, send(3, _, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{static_cast<ssize_t>(packet_data_.length()), 0}));

  writePacketAndVerifyResult(quic::WRITE_STATUS_OK);
}

// Tests write with NoBufferSpace error returns WRITE_STATUS_BLOCKED and retries with exponential
// backoff: 1 retry -> 1ms, 2 retries -> 2ms, 3 retries -> 4ms, etc.
TEST_F(QuicPlatformPacketWriterFactoryTest,
       WritePacketWithNoBufferSpaceErrorAndRetryWithExponentialBackoff) {
  EXPECT_CALL(os_sys_calls_, send(3, _, _, _))
      .WillRepeatedly(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_NOBUFS}));

  // Mock the retry timer.
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(1), _));
  writePacketAndVerifyResult(quic::WRITE_STATUS_BLOCKED);

  // Simulate timer firing to allow retry up to 12 times.
  for (size_t retry_count = 1; retry_count < 12; ++retry_count) {
    EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write)).WillOnce(Invoke([&]() {
      std::cerr << "activate write event for retry count " << retry_count << "\n";
      EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(1 << retry_count), _));
      packet_writer_->SetWritable();
      writePacketAndVerifyResult(quic::WRITE_STATUS_BLOCKED);
    }));
    timer->invokeCallback();
  }
  // On 12th retry, expect WRITE_STATUS_ERROR to be propagated.
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write)).WillOnce(Invoke([&]() {
    packet_writer_->SetWritable();
    writePacketAndVerifyResult(quic::WRITE_STATUS_ERROR, SOCKET_ERROR_NOBUFS);
  }));
  timer->invokeCallback();
}

// Test that retry count gets reset after a successful write.
TEST_F(QuicPlatformPacketWriterFactoryTest, RetryCountResetUponSuccessfulWrite) {
  EXPECT_CALL(os_sys_calls_, send(3, _, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_NOBUFS}));

  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(1), _));
  writePacketAndVerifyResult(quic::WRITE_STATUS_BLOCKED);

  // Simulate timer firing to allow retry with successful write.
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write)).WillOnce(Invoke([&]() {
    packet_writer_->SetWritable();
    // Simulate successful write after retries.
    EXPECT_CALL(os_sys_calls_, send(3, _, _, _))
        .WillOnce(Return(Api::SysCallSizeResult{static_cast<ssize_t>(packet_data_.length()), 0}));
    packet_writer_->SetWritable();
    writePacketAndVerifyResult(quic::WRITE_STATUS_OK);
  }));
  timer->invokeCallback();

  // Retry count should be reset after the last successful write. Following SOCKET_ERROR_NOBUFS
  // errors should be retried for 12 times again.
  EXPECT_CALL(os_sys_calls_, send(3, _, _, _))
      .WillRepeatedly(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_NOBUFS}));
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(1), _));
  writePacketAndVerifyResult(quic::WRITE_STATUS_BLOCKED);
  // Following 11 retries would also fail with SOCKET_ERROR_NOBUFS, but they all should be retried.
  for (size_t retry_count = 1; retry_count < 12; ++retry_count) {
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(1 << retry_count), _));
    EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write)).WillOnce(Invoke([&]() {
      packet_writer_->SetWritable();
      writePacketAndVerifyResult(quic::WRITE_STATUS_BLOCKED);
    }));
    timer->invokeCallback();
  }
  // On 12th retry, expect WRITE_STATUS_ERROR to be propagated.
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write)).WillOnce(Invoke([&]() {
    packet_writer_->SetWritable();
    writePacketAndVerifyResult(quic::WRITE_STATUS_ERROR, SOCKET_ERROR_NOBUFS);
  }));
  timer->invokeCallback();
}

// Test that other error codes don't trigger retry logic
TEST_F(QuicPlatformPacketWriterFactoryTest, OtherErrorCodesNoRetry) {
  // EPERM should not trigger retry
  EXPECT_CALL(os_sys_calls_, send(3, _, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_PERM}));

  writePacketAndVerifyResult(quic::WRITE_STATUS_ERROR, SOCKET_ERROR_PERM);
}

} // namespace Quic
} // namespace Envoy
