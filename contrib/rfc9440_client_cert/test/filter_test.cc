#include "source/extensions/filters/http/rfc9440_client_cert/filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Rfc9440ClientCert {

class Rfc9440ClientCertFilterTest : public testing::Test {
public:
  Rfc9440ClientCertFilterTest()
      : config_(std::make_shared<Rfc9440ClientCertFilterConfig>(true)), filter_(config_) {
    filter_.setDecoderFilterCallbacks(callbacks_);
    ON_CALL(callbacks_, connection())
        .WillByDefault(Return(OptRef<const Network::Connection>(connection_)));
    ON_CALL(connection_, ssl()).WillByDefault(Return(ssl_));
  }

  Rfc9440ClientCertFilterConfigSharedPtr config_;
  Rfc9440ClientCertFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Network::MockConnection> connection_;
  std::shared_ptr<NiceMock<Ssl::MockConnectionInfo>> ssl_{
      std::make_shared<NiceMock<Ssl::MockConnectionInfo>>()};
};

TEST_F(Rfc9440ClientCertFilterTest, StripsIncomingAndSetsNewHeadersWithMultiItemChain) {
  Http::TestRequestHeaderMapImpl headers{{"client-cert", "spoofed_leaf_data"},
                                         {"client-cert-chain", "spoofed_chain_data"},
                                         {"user-agent", "curl"}};

  std::string mock_leaf =
      "-----BEGIN CERTIFICATE-----\nYmFzZTY0X2xlYWZfY2VydA==\n-----END CERTIFICATE-----\n";

  std::vector<std::string> mock_chain = {
      mock_leaf, "-----BEGIN CERTIFICATE-----\nintermediate1\n-----END CERTIFICATE-----",
      "-----BEGIN CERTIFICATE-----\nintermediate2\n-----END CERTIFICATE-----"};

  EXPECT_CALL(*ssl_, pemEncodedPeerCertificate()).WillOnce(ReturnRef(mock_leaf));
  EXPECT_CALL(*ssl_, pemEncodedPeerCertificateChain()).WillOnce(Return(mock_chain));

  Http::FilterHeadersStatus status = filter_.decodeHeaders(headers, false);
  EXPECT_EQ(status, Http::FilterHeadersStatus::Continue);

  EXPECT_EQ(headers.get_("client-cert"), ":YmFzZTY0X2xlYWZfY2VydA==:");
  EXPECT_EQ(headers.get_("client-cert-chain"), ":intermediate1:, :intermediate2:");
  EXPECT_EQ(headers.get_("user-agent"), "curl");
}

TEST_F(Rfc9440ClientCertFilterTest, NonTlsConnectionSanitizesSpoofedHeaders) {
  Http::TestRequestHeaderMapImpl headers{{"client-cert", "evil_spoofed_leaf"},
                                         {"client-cert-chain", "evil_spoofed_chain"},
                                         {"user-agent", "curl"}};

  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));

  EXPECT_CALL(*ssl_, pemEncodedPeerCertificate()).Times(0);
  EXPECT_CALL(*ssl_, pemEncodedPeerCertificateChain()).Times(0);

  Http::FilterHeadersStatus status = filter_.decodeHeaders(headers, false);
  EXPECT_EQ(status, Http::FilterHeadersStatus::Continue);

  EXPECT_TRUE(headers.get_("client-cert").empty());
  EXPECT_TRUE(headers.get_("client-cert-chain").empty());
  EXPECT_EQ(headers.get_("user-agent"), "curl");
}

TEST_F(Rfc9440ClientCertFilterTest, EmptyLeafCertificateSanitizesHeadersAndDoesNotReadd) {
  Http::TestRequestHeaderMapImpl headers{{"client-cert", "evil_spoofed_leaf"},
                                         {"client-cert-chain", "evil_spoofed_chain"},
                                         {"user-agent", "curl"}};

  std::string empty_leaf = "";
  std::vector<std::string> empty_chain = {};

  EXPECT_CALL(*ssl_, pemEncodedPeerCertificate()).WillOnce(ReturnRef(empty_leaf));
  EXPECT_CALL(*ssl_, pemEncodedPeerCertificateChain()).WillOnce(Return(empty_chain));

  Http::FilterHeadersStatus status = filter_.decodeHeaders(headers, false);
  EXPECT_EQ(status, Http::FilterHeadersStatus::Continue);

  EXPECT_TRUE(headers.get_("client-cert").empty());
  EXPECT_TRUE(headers.get_("client-cert-chain").empty());
  EXPECT_EQ(headers.get_("user-agent"), "curl");
}

TEST_F(Rfc9440ClientCertFilterTest, EmptyChainDoesNotAddChainHeader) {
  Http::TestRequestHeaderMapImpl headers{{"client-cert-chain", "evil_spoofed_chain"},
                                         {"user-agent", "curl"}};

  std::string mock_leaf =
      "-----BEGIN CERTIFICATE-----\nYmFzZTY0X2xlYWZfY2VydA==\n-----END CERTIFICATE-----\n";
  std::vector<std::string> empty_chain = {};

  EXPECT_CALL(*ssl_, pemEncodedPeerCertificate()).WillOnce(ReturnRef(mock_leaf));
  EXPECT_CALL(*ssl_, pemEncodedPeerCertificateChain()).WillOnce(Return(empty_chain));

  Http::FilterHeadersStatus status = filter_.decodeHeaders(headers, false);
  EXPECT_EQ(status, Http::FilterHeadersStatus::Continue);

  EXPECT_EQ(headers.get_("client-cert"), ":YmFzZTY0X2xlYWZfY2VydA==:");
  EXPECT_TRUE(headers.get_("client-cert-chain").empty());
}

TEST_F(Rfc9440ClientCertFilterTest, ConfigFalseOmitsChainHeader) {
  Http::TestRequestHeaderMapImpl headers{{"user-agent", "curl"}};

  std::string mock_leaf =
      "-----BEGIN CERTIFICATE-----\nYmFzZTY0X2xlYWZfY2VydA==\n-----END CERTIFICATE-----\n";
  std::vector<std::string> mock_chain = {
      mock_leaf, "-----BEGIN CERTIFICATE-----\nintermediate1\n-----END CERTIFICATE-----"};

  auto disabled_config = std::make_shared<Rfc9440ClientCertFilterConfig>(false);
  Rfc9440ClientCertFilter disabled_filter(disabled_config);
  disabled_filter.setDecoderFilterCallbacks(callbacks_);

  EXPECT_CALL(*ssl_, pemEncodedPeerCertificate()).WillOnce(ReturnRef(mock_leaf));

  Http::FilterHeadersStatus status = disabled_filter.decodeHeaders(headers, false);
  EXPECT_EQ(status, Http::FilterHeadersStatus::Continue);

  EXPECT_EQ(headers.get_("client-cert"), ":YmFzZTY0X2xlYWZfY2VydA==:");
  EXPECT_TRUE(headers.get_("client-cert-chain").empty());
}

} // namespace Rfc9440ClientCert
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
