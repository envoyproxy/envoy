#include "envoy/extensions/filters/udp/udp_proxy/session/http_capsule/v3/http_capsule.pb.h"

#include "source/extensions/filters/udp/udp_proxy/session_filters/http_capsule/http_capsule.h"

#include "test/extensions/filters/udp/udp_proxy/mocks.h"
#include "test/mocks/server/factory_context.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace HttpCapsule {
namespace {

class HttpCapsuleFilterTest : public testing::Test {
public:
  void setup() {
    filter_ = std::make_unique<HttpCapsuleFilter>(server_context_.timeSource());
    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
  }

  NiceMock<Server::Configuration::MockFactoryContext> server_context_;
  std::unique_ptr<HttpCapsuleFilter> filter_;
  NiceMock<MockReadFilterCallbacks> read_callbacks_;
  NiceMock<MockWriteFilterCallbacks> write_callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(HttpCapsuleFilterTest, ContinueOnNewSession) {
  setup();
  EXPECT_EQ(ReadFilterStatus::Continue, filter_->onNewSession());
}

TEST_F(HttpCapsuleFilterTest, EncapsulateEmptyDatagram) {
  setup();

  Network::UdpRecvData datagram;
  datagram.buffer_ = std::make_unique<Buffer::OwnedImpl>();
  EXPECT_EQ(ReadFilterStatus::Continue, filter_->onData(datagram));

  const std::string expected_data = absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                                                           "01" // Capsule length
                                                           "00" // Context ID
  );

  EXPECT_EQ(expected_data, datagram.buffer_->toString());
}

TEST_F(HttpCapsuleFilterTest, EncapsulateDatagram) {
  setup();

  const std::string payload = "payload";
  Network::UdpRecvData datagram;
  datagram.buffer_ = std::make_unique<Buffer::OwnedImpl>();
  datagram.buffer_->add(payload);
  EXPECT_EQ(ReadFilterStatus::Continue, filter_->onData(datagram));

  const std::string expected_data =
      absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                             "08" // Capsule Length = length(payload) + 1
                             "00" // Context ID
                             ) +
      payload;

  EXPECT_EQ(expected_data, datagram.buffer_->toString());
}

TEST_F(HttpCapsuleFilterTest, InvalidCapsule) {
  setup();

  const std::string invalid_context_id_fragment =
      absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                             "01" // Capsule Length
                             "c0" // Context ID (Invalid VarInt62)
      );

  Network::UdpRecvData datagram;
  datagram.buffer_ = std::make_unique<Buffer::OwnedImpl>();
  datagram.buffer_->add(invalid_context_id_fragment);
  EXPECT_CALL(write_callbacks_, injectDatagramToFilterChain(_)).Times(0);
  EXPECT_EQ(WriteFilterStatus::StopIteration, filter_->onWrite(datagram));
  EXPECT_EQ(0, datagram.buffer_->length());
}

TEST_F(HttpCapsuleFilterTest, IncompatibleCapsule) {
  setup();

  const std::string unexpected_capsule_fragment =
      absl::HexStringToBytes("17" // Capsule Type is not DATAGRAM
                             "01" // Capsule Length
                             "00" // Unknown capsule payload
      );

  Network::UdpRecvData datagram;
  datagram.buffer_ = std::make_unique<Buffer::OwnedImpl>();
  datagram.buffer_->add(unexpected_capsule_fragment);
  EXPECT_CALL(write_callbacks_, injectDatagramToFilterChain(_)).Times(0);
  EXPECT_EQ(WriteFilterStatus::StopIteration, filter_->onWrite(datagram));
  EXPECT_EQ(0, datagram.buffer_->length());
}

TEST_F(HttpCapsuleFilterTest, UnknownContextId) {
  setup();

  const std::string invalid_context_id_fragment =
      absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                             "01" // Capsule Length
                             "01" // Unknown Context ID
      );

  Network::UdpRecvData datagram;
  datagram.buffer_ = std::make_unique<Buffer::OwnedImpl>();
  datagram.buffer_->add(invalid_context_id_fragment);
  EXPECT_CALL(write_callbacks_, injectDatagramToFilterChain(_)).Times(0);
  EXPECT_EQ(WriteFilterStatus::StopIteration, filter_->onWrite(datagram));
  EXPECT_EQ(0, datagram.buffer_->length());
}

TEST_F(HttpCapsuleFilterTest, DecapsulateDatagram) {
  setup();

  const std::string payload = "payload";
  const std::string capsule = absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                                                     "08" // Capsule Length = length(payload) + 1
                                                     "00" // Context ID
                                                     ) +
                              payload;

  Network::UdpRecvData datagram;
  datagram.buffer_ = std::make_unique<Buffer::OwnedImpl>();
  datagram.buffer_->add(capsule);
  EXPECT_CALL(write_callbacks_, injectDatagramToFilterChain(_))
      .WillOnce(Invoke([payload](Network::UdpRecvData& data) -> void {
        EXPECT_EQ(payload, data.buffer_->toString());
      }));
  EXPECT_EQ(WriteFilterStatus::StopIteration, filter_->onWrite(datagram));
  EXPECT_EQ(0, datagram.buffer_->length());
}

TEST_F(HttpCapsuleFilterTest, DecapsulateSplitPayload) {
  setup();

  const std::string payload = "payload";
  const std::string capsule = absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                                                     "08" // Capsule Length = length(payload) + 1
                                                     "00" // Context ID
                                                     ) +
                              payload;

  int pivot_index = 4; // Some arbitrary split index of the capsule.

  // Send first part of the capsule and verify that a datagram was not injected.
  Network::UdpRecvData payload1;
  payload1.buffer_ = std::make_unique<Buffer::OwnedImpl>();
  payload1.buffer_->add(capsule.substr(0, pivot_index));
  EXPECT_CALL(write_callbacks_, injectDatagramToFilterChain(_)).Times(0);
  EXPECT_EQ(WriteFilterStatus::StopIteration, filter_->onWrite(payload1));
  EXPECT_EQ(0, payload1.buffer_->length());

  // Send second part of the capsule and verify that a datagram was generated.
  Network::UdpRecvData payload2;
  payload2.buffer_ = std::make_unique<Buffer::OwnedImpl>();
  payload2.buffer_->add(capsule.substr(pivot_index));
  EXPECT_CALL(write_callbacks_, injectDatagramToFilterChain(_))
      .WillOnce(Invoke([payload](Network::UdpRecvData& data) -> void {
        EXPECT_EQ(payload, data.buffer_->toString());
      }));
  EXPECT_EQ(WriteFilterStatus::StopIteration, filter_->onWrite(payload2));
  EXPECT_EQ(0, payload2.buffer_->length());
}

TEST_F(HttpCapsuleFilterTest, DecapsulateMultipleDatagrams) {
  setup();

  const std::string capsule = absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                                                     "09" // Capsule Length = length(payload1) + 1
                                                     "00" // Context ID
                                                     ) +
                              "payload1" +
                              absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                                                     "09" // Capsule Length = length(payload2) + 1
                                                     "00" // Context ID
                                                     ) +
                              "payload2";

  Network::UdpRecvData datagram;
  datagram.buffer_ = std::make_unique<Buffer::OwnedImpl>();
  datagram.buffer_->add(capsule);
  EXPECT_CALL(write_callbacks_, injectDatagramToFilterChain(_))
      .WillOnce(Invoke([](Network::UdpRecvData& data) -> void {
        EXPECT_EQ("payload1", data.buffer_->toString());
      }))
      .WillOnce(Invoke([](Network::UdpRecvData& data) -> void {
        EXPECT_EQ("payload2", data.buffer_->toString());
      }));

  EXPECT_EQ(WriteFilterStatus::StopIteration, filter_->onWrite(datagram));
  EXPECT_EQ(0, datagram.buffer_->length());
}

} // namespace
} // namespace HttpCapsule
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
