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
                                      std::shared_ptr<QuicSslConnectionInfo>&& ssl_info,
                                      QuicStatNames& quic_stat_names, Stats::Scope& scope)
      : QuicFilterManagerConnectionImpl(
            connection, connection_id, dispatcher, send_buffer_limit, std::move(ssl_info),
            std::make_unique<StreamInfo::StreamInfoImpl>(
                dispatcher.timeSource(),
                connection.connectionSocket()->connectionInfoProviderSharedPtr(),
                StreamInfo::FilterState::LifeSpan::Connection),
            quic_stat_names, scope) {}

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
        quic_stat_names_(store_.symbolTable()), connection_(std::move(socket_)),
        quic_session_(new quic::test::MockQuicConnection(&helper_, &alarm_factory_,
                                                         quic::Perspective::IS_SERVER)),
        ssl_info_(std::make_shared<QuicSslConnectionInfo>(quic_session_)),
        impl_(connection_, connection_id_, dispatcher_, send_buffer_limit_, std::move(ssl_info_),
              quic_stat_names_, *store_.rootScope()) {}

protected:
  std::unique_ptr<NiceMock<Network::MockConnectionSocket>> socket_;
  const quic::QuicConnectionId connection_id_;
  Event::MockDispatcher dispatcher_;
  uint32_t send_buffer_limit_ = 0;
  quic::test::MockQuicConnectionHelper helper_;
  quic::test::MockAlarmFactory alarm_factory_;
  Stats::IsolatedStoreImpl store_;
  QuicStatNames quic_stat_names_;
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

TEST_F(QuicFilterManagerConnectionImplTest, UpdateBytesBuffered) {
  impl_.updateBytesBuffered(0, 110);
  EXPECT_EQ(110u, impl_.bytesToSend());
  impl_.updateBytesBuffered(110, 0);
  EXPECT_EQ(0u, impl_.bytesToSend());
}

TEST_F(QuicFilterManagerConnectionImplTest, AddBytesSentCallback) {
  Network::Connection::BytesSentCb cb;
  EXPECT_ENVOY_BUG(impl_.addBytesSentCallback(cb), "unexpected call to addBytesSentCallback");
}

TEST_F(QuicFilterManagerConnectionImplTest, NoDelay) {
  // This is a no-op, but call it for test coverage.
  impl_.noDelay(false);
}

TEST_F(QuicFilterManagerConnectionImplTest, ReadDisable) {
  EXPECT_ENVOY_BUG(impl_.readDisable(true), "Unexpected call to readDisable");
}

TEST_F(QuicFilterManagerConnectionImplTest, DetectEarlyCloseWhenReadDisabled) {
  EXPECT_ENVOY_BUG(impl_.detectEarlyCloseWhenReadDisabled(true),
                   "Unexpected call to detectEarlyCloseWhenReadDisabled");
}

TEST_F(QuicFilterManagerConnectionImplTest, UnixSocketPeerCredentials) {
  // This is a no-op, but call it for test coverage.
  EXPECT_FALSE(impl_.unixSocketPeerCredentials().has_value());
}

TEST_F(QuicFilterManagerConnectionImplTest, Write) {
  Buffer::OwnedImpl data;
  EXPECT_ENVOY_BUG(impl_.write(data, true), "unexpected write call");
}

TEST_F(QuicFilterManagerConnectionImplTest, RawWrite) {
  Buffer::OwnedImpl data;
  EXPECT_ENVOY_BUG(impl_.rawWrite(data, true), "unexpected call to rawWrite");
}

TEST_F(QuicFilterManagerConnectionImplTest, BufferLimit) {
  EXPECT_DEATH(impl_.bufferLimit(), "not implemented");
}

TEST_F(QuicFilterManagerConnectionImplTest, SetBufferLimits) {
  EXPECT_ENVOY_BUG(impl_.setBufferLimits(1), "unexpected call to setBufferLimits");
}

TEST_F(QuicFilterManagerConnectionImplTest, GetWriteBuffer) {
  EXPECT_DEATH(impl_.getWriteBuffer(), "not implemented");
}

TEST_F(QuicFilterManagerConnectionImplTest, EnableHalfClose) {
  impl_.enableHalfClose(false); // No-op
  EXPECT_DEATH(impl_.enableHalfClose(true), "Quic connection doesn't support half close.");
}

TEST_F(QuicFilterManagerConnectionImplTest, IsHalfCloseEnabled) {
  EXPECT_FALSE(impl_.isHalfCloseEnabled());
}

TEST_F(QuicFilterManagerConnectionImplTest, StreamInfoConnectionId) {
  const absl::optional<uint64_t> id = impl_.connectionInfoProvider().connectionID();
  EXPECT_TRUE(id.has_value());
  EXPECT_NE(id.value_or(0), 0);
}

} // namespace Quic
} // namespace Envoy
