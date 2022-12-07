#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/socket.h"

#include "test/integration/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "gtest/gtest.h"
#include "gtest/gtest_pred_impl.h"

namespace Envoy {
/**
 * TCP client used during integration testing.
 */
class IntegrationTcpClient {
public:
  IntegrationTcpClient(Event::Dispatcher& dispatcher, MockBufferFactory& factory, uint32_t port,
                       Network::Address::IpVersion version, bool enable_half_close,
                       const Network::ConnectionSocket::OptionsSharedPtr& options,
                       Network::Address::InstanceConstSharedPtr source_address = nullptr,
                       absl::string_view destination_address = "");

  void close();
  void waitForData(const std::string& data, bool exact_match = true);
  // wait for at least `length` bytes to be received
  ABSL_MUST_USE_RESULT AssertionResult
  waitForData(size_t length, std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);
  void waitForDisconnect(bool ignore_spurious_events = false);
  void waitForHalfClose();
  void readDisable(bool disabled);
  ABSL_MUST_USE_RESULT AssertionResult
  write(const std::string& data, bool end_stream = false, bool verify = true,
        std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);
  const std::string& data() { return payload_reader_->data(); }
  bool connected() const { return !disconnected_; }
  // clear up to the `count` number of bytes of received data
  void clearData(size_t count = std::string::npos) { payload_reader_->clearData(count); }

private:
  struct ConnectionCallbacks : public Network::ConnectionCallbacks {
    ConnectionCallbacks(IntegrationTcpClient& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    IntegrationTcpClient& parent_;
  };

  std::shared_ptr<WaitForPayloadReader> payload_reader_;
  std::shared_ptr<ConnectionCallbacks> callbacks_;
  Network::ClientConnectionPtr connection_;
  bool disconnected_{};
  MockWatermarkBuffer* client_write_buffer_;
};

using IntegrationTcpClientPtr = std::unique_ptr<IntegrationTcpClient>;

} // namespace Envoy
