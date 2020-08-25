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

#include "common/buffer/buffer_impl.h"
#include "common/common/fmt.h"
#include "common/network/utility.h"

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
    const Network::ConnectionSocket::OptionsSharedPtr& options)
    : payload_reader_(new WaitForPayloadReader(dispatcher)),
      callbacks_(new ConnectionCallbacks(*this)) {
  EXPECT_CALL(factory, create_(_, _, _))
      .WillOnce(Invoke([&](std::function<void()> below_low, std::function<void()> above_high,
                           std::function<void()> above_overflow) -> Buffer::Instance* {
        client_write_buffer_ =
            new NiceMock<MockWatermarkBuffer>(below_low, above_high, above_overflow);
        return client_write_buffer_;
      }));

  connection_ = dispatcher.createClientConnection(
      Network::Utility::resolveUrl(
          fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version), port)),
      Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(), options);

  ON_CALL(*client_write_buffer_, drain(_))
      .WillByDefault(testing::Invoke(client_write_buffer_, &MockWatermarkBuffer::baseDrain));
  EXPECT_CALL(*client_write_buffer_, drain(_)).Times(AnyNumber());

  connection_->enableHalfClose(enable_half_close);
  connection_->addConnectionCallbacks(*callbacks_);
  connection_->addReadFilter(payload_reader_);
  connection_->connect();
}

void IntegrationTcpClient::close() { connection_->close(Network::ConnectionCloseType::NoFlush); }

void IntegrationTcpClient::waitForData(const std::string& data, bool exact_match) {
  auto found = payload_reader_->data().find(data);
  if (found == 0 || (!exact_match && found != std::string::npos)) {
    return;
  }

  payload_reader_->set_data_to_wait_for(data, exact_match);
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

void IntegrationTcpClient::waitForHalfClose() {
  if (payload_reader_->readLastByte()) {
    return;
  }
  connection_->dispatcher().run(Event::Dispatcher::RunType::Block);
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
      EXPECT_CALL(*client_write_buffer_, write(_)).Times(AtLeast(1));
    }
  }

  int bytes_expected = client_write_buffer_->bytes_written() + data.size();

  connection_->write(buffer, end_stream);
  do {
    connection_->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
    if (client_write_buffer_->bytes_written() == bytes_expected || disconnected_) {
      break;
    }
  } while (bound.withinBound());

  if (!bound.withinBound()) {
    return AssertionFailure() << "Timed out completing write";
  } else if (verify && (disconnected_ || client_write_buffer_->bytes_written() != bytes_expected)) {
    return AssertionFailure()
           << "Failed to complete write or unexpected disconnect. disconnected_: " << disconnected_
           << " bytes_written: " << client_write_buffer_->bytes_written()
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
