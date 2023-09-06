#include "test/integration/integration_tcp_client.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>

#include "envoy/buffer/buffer.h"
#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/fmt.h"
#include "source/common/network/utility.h"

#include "test/integration/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/test_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;
using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::NiceMock;

IntegrationTcpClient::IntegrationTcpClient(
    Event::Dispatcher& dispatcher, MockBufferFactory& factory, uint32_t port,
    Network::Address::IpVersion version, bool enable_half_close,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    Network::Address::InstanceConstSharedPtr source_address, absl::string_view destination_address)
    : payload_reader_(new WaitForPayloadReader(dispatcher)),
      callbacks_(new ConnectionCallbacks(*this)) {
  EXPECT_CALL(factory, createBuffer_(_, _, _))
      .Times(AtLeast(1))
      .WillOnce(Invoke([&](std::function<void()> below_low, std::function<void()> above_high,
                           std::function<void()> above_overflow) -> Buffer::Instance* {
        client_write_buffer_ =
            new NiceMock<MockWatermarkBuffer>(below_low, above_high, above_overflow);
        return client_write_buffer_;
      }))
      .WillRepeatedly(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                                std::function<void()> above_overflow) -> Buffer::Instance* {
        return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
      }));

  connection_ = dispatcher.createClientConnection(
      Network::Utility::resolveUrl(fmt::format(
          "tcp://{}:{}",
          destination_address.empty() ? Network::Test::getLoopbackAddressUrlString(version)
                                      : destination_address,
          port)),
      source_address, Network::Test::createRawBufferSocket(), options, nullptr);

  ON_CALL(*client_write_buffer_, drain(_))
      .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
  EXPECT_CALL(*client_write_buffer_, drain(_)).Times(AnyNumber());

  connection_->enableHalfClose(enable_half_close);
  connection_->addConnectionCallbacks(*callbacks_);
  connection_->addReadFilter(payload_reader_);
  connection_->connect();
}

void IntegrationTcpClient::close() { connection_->close(Network::ConnectionCloseType::NoFlush); }

void IntegrationTcpClient::close(Network::ConnectionCloseType close_type) {
  connection_->close(close_type);
}

void IntegrationTcpClient::waitForData(const std::string& data, bool exact_match) {
  auto found = payload_reader_->data().find(data);
  if (found == 0 || (!exact_match && found != std::string::npos)) {
    return;
  }

  payload_reader_->setDataToWaitFor(data, exact_match);
  connection_->dispatcher().run(Event::Dispatcher::RunType::Block);
}

AssertionResult IntegrationTcpClient::waitForData(size_t length,
                                                  std::chrono::milliseconds timeout) {
  if (payload_reader_->data().size() >= length) {
    return AssertionSuccess();
  }

  return payload_reader_->waitForLength(length, timeout);
}

void IntegrationTcpClient::waitForDisconnect(bool ignore_spurious_events) {
  if (disconnected_) {
    return;
  }
  Event::TimerPtr timeout_timer =
      connection_->dispatcher().createTimer([this]() -> void { connection_->dispatcher().exit(); });
  timeout_timer->enableTimer(TestUtility::DefaultTimeout);

  if (ignore_spurious_events) {
    while (!disconnected_ && timeout_timer->enabled()) {
      connection_->dispatcher().run(Event::Dispatcher::RunType::Block);
    }
  } else {
    connection_->dispatcher().run(Event::Dispatcher::RunType::Block);
  }
  EXPECT_TRUE(disconnected_);
}

void IntegrationTcpClient::waitForHalfClose(bool ignore_spurious_events) {
  waitForHalfClose(TestUtility::DefaultTimeout, ignore_spurious_events);
}

void IntegrationTcpClient::waitForHalfClose(std::chrono::milliseconds timeout,
                                            bool ignore_spurious_events) {
  if (payload_reader_->readLastByte()) {
    return;
  }
  Event::TimerPtr timeout_timer =
      connection_->dispatcher().createTimer([this]() -> void { connection_->dispatcher().exit(); });
  timeout_timer->enableTimer(timeout);

  if (ignore_spurious_events) {
    while (!payload_reader_->readLastByte() && timeout_timer->enabled()) {
      connection_->dispatcher().run(Event::Dispatcher::RunType::Block);
    }
  } else {
    connection_->dispatcher().run(Event::Dispatcher::RunType::Block);
  }

  EXPECT_TRUE(payload_reader_->readLastByte());
}

void IntegrationTcpClient::readDisable(bool disabled) { connection_->readDisable(disabled); }

AssertionResult IntegrationTcpClient::write(const std::string& data, bool end_stream, bool verify,
                                            std::chrono::milliseconds timeout) {
  Event::TestTimeSystem::RealTimeBound bound(timeout);
  Buffer::OwnedImpl buffer(data);
  if (verify) {
    EXPECT_CALL(*client_write_buffer_, move(_));
    if (!data.empty()) {
      EXPECT_CALL(*client_write_buffer_, drain(_)).Times(AtLeast(1));
    }
  }

  uint64_t bytes_expected = client_write_buffer_->bytesDrained() + data.size();

  connection_->write(buffer, end_stream);
  do {
    connection_->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
    if (client_write_buffer_->bytesDrained() == bytes_expected || disconnected_) {
      break;
    }
  } while (bound.withinBound());

  if (!bound.withinBound()) {
    return AssertionFailure() << "Timed out completing write";
  } else if (verify && (disconnected_ || client_write_buffer_->bytesDrained() != bytes_expected)) {
    return AssertionFailure()
           << "Failed to complete write or unexpected disconnect. disconnected_: " << disconnected_
           << " bytes_drained: " << client_write_buffer_->bytesDrained()
           << " bytes_expected: " << bytes_expected;
  }

  return AssertionSuccess();
}

void IntegrationTcpClient::ConnectionCallbacks::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose) {
    parent_.disconnected_ = true;
    parent_.connection_->dispatcher().exit();
  }
}

} // namespace Envoy
