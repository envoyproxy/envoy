#include "source/common/quic/quic_filter_manager_connection_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace Envoy {
namespace Quic {

class TestQuicFilterManagerConnectionImpl : public QuicFilterManagerConnectionImpl {
public:
  TestQuicFilterManagerConnectionImpl(QuicNetworkConnection& connection,
                                      const quic::QuicConnectionId& connection_id,
                                      Event::Dispatcher& dispatcher, uint32_t send_buffer_limit,
                                      std::shared_ptr<QuicSslConnectionInfo>&& ssl_info)
      : QuicFilterManagerConnectionImpl(connection, connection_id, dispatcher, send_buffer_limit,
                                        std::move(ssl_info)) {}

  void dumpState(std::ostream& /*os*/, int /*indent_level = 0*/) const override {}
  absl::string_view requestedServerName() const override { return {}; }
  bool hasDataToWrite() override { return false; }
  const quic::QuicConnection* quicConnection() const override { return nullptr; }
  quic::QuicConnection* quicConnection() override { return nullptr; }

  void closeNow() {
    const quic::QuicConnectionCloseFrame frame;
    quic::ConnectionCloseSource source = quic::ConnectionCloseSource::FROM_SELF;
    const quic::ParsedQuicVersion version = quic::AllSupportedVersions().front();
    onConnectionCloseEvent(frame, source, version);
  }
};

class QuicFilterManagerConnectionImplTest : public ::testing::Test {
public:
  QuicFilterManagerConnectionImplTest()
      : socket_(std::make_unique<NiceMock<Network::MockConnectionSocket>>()),
        connection_(std::move(socket_)),
        quic_session_(new quic::test::MockQuicConnection(&helper_, &alarm_factory_,
                                                         quic::Perspective::IS_SERVER)),
        ssl_info_(std::make_shared<QuicSslConnectionInfo>(quic_session_)),
        impl_(connection_, connection_id_, dispatcher_, send_buffer_limit_, std::move(ssl_info_)) {}

protected:
  std::unique_ptr<NiceMock<Network::MockConnectionSocket>> socket_;
  const quic::QuicConnectionId connection_id_;
  Event::MockDispatcher dispatcher_;
  uint32_t send_buffer_limit_ = 0;
  quic::test::MockQuicConnectionHelper helper_;
  quic::test::MockAlarmFactory alarm_factory_;
  QuicNetworkConnection connection_;
  quic::test::MockQuicSession quic_session_;
  std::shared_ptr<QuicSslConnectionInfo> ssl_info_;
  TestQuicFilterManagerConnectionImpl impl_;
};

TEST_F(QuicFilterManagerConnectionImplTest, ConnectionInfoProviderSharedPtr) {
  EXPECT_TRUE(impl_.connectionInfoProviderSharedPtr() != nullptr);
  impl_.closeNow();
  EXPECT_TRUE(impl_.connectionInfoProviderSharedPtr() == nullptr);
}

} // namespace Quic
} // namespace Envoy
