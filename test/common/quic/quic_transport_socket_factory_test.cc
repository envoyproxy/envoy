#include "source/common/quic/quic_transport_socket_factory.h"

#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/mocks/ssl/mocks.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Quic {

class QuicClientTransportSocketFactoryTest : public testing::Test {
public:
  QuicClientTransportSocketFactoryTest() {
    EXPECT_CALL(context_.context_manager_, createSslClientContext(_, _)).WillOnce(Return(nullptr));
    EXPECT_CALL(*context_config_, setSecretUpdateCallback(_))
        .WillOnce(testing::SaveArg<0>(&update_callback_));
    factory_.emplace(std::unique_ptr<Envoy::Ssl::ClientContextConfig>(context_config_), context_);
    factory_->initialize();
  }

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  absl::optional<Quic::QuicClientTransportSocketFactory> factory_;
  // Will be owned by factory_.
  NiceMock<Ssl::MockClientContextConfig>* context_config_{
      new NiceMock<Ssl::MockClientContextConfig>};
  std::function<void()> update_callback_;
};

TEST_F(QuicClientTransportSocketFactoryTest, getCryptoConfig) {
  EXPECT_EQ(nullptr, factory_->getCryptoConfig());

  Ssl::ClientContextSharedPtr ssl_context1{new Ssl::MockClientContext()};
  EXPECT_CALL(context_.context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(ssl_context1));
  update_callback_();
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config1 = factory_->getCryptoConfig();
  EXPECT_NE(nullptr, crypto_config1);

  Ssl::ClientContextSharedPtr ssl_context2{new Ssl::MockClientContext()};
  EXPECT_CALL(context_.context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(ssl_context2));
  update_callback_();
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config2 = factory_->getCryptoConfig();
  EXPECT_NE(crypto_config2, crypto_config1);
}

} // namespace Quic
} // namespace Envoy
