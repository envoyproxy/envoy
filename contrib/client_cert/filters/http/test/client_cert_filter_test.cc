#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/client_cert/filters/http/source/client_cert_filter.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ClientCert {
namespace {

class ClientCertFilterTest : public testing::Test {
public:
  ClientCertFilterTest() : filter_(makeFilter(true)) {
    ON_CALL(callbacks_, connection())
        .WillByDefault(Return(OptRef<const Network::Connection>(connection_)));
    ON_CALL(connection_, ssl()).WillByDefault(Return(ssl_));
  }

  ClientCertFilter makeFilter(bool set_client_cert_chain) {
    envoy::extensions::filters::http::client_cert::v3alpha::ClientCertConfig proto_config;
    proto_config.set_set_client_cert_chain(set_client_cert_chain);
    ClientCertFilter filter(std::make_shared<ClientCertFilterConfig>(proto_config));
    filter.setDecoderFilterCallbacks(callbacks_);
    return filter;
  }

  const std::string leaf_pem_{
      "-----BEGIN CERTIFICATE-----\nYmFzZTY0X2xlYWZfY2VydA==\n-----END CERTIFICATE-----\n"};
  const std::string empty_pem_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Network::MockConnection> connection_;
  std::shared_ptr<NiceMock<Ssl::MockConnectionInfo>> ssl_{
      std::make_shared<NiceMock<Ssl::MockConnectionInfo>>()};
  // Must be declared after the mocks: makeFilter() binds callbacks_.
  ClientCertFilter filter_;
};

TEST_F(ClientCertFilterTest, StripsIncomingAndSetsHeadersFromValidatedChain) {
  Http::TestRequestHeaderMapImpl headers{{"client-cert", "spoofed_leaf_data"},
                                         {"client-cert-chain", "spoofed_chain_data"},
                                         {"user-agent", "curl"}};

  // The validated chain is ordered leaf-first; the leaf must not be repeated in the
  // Client-Cert-Chain header.
  const std::vector<std::string> validated_chain = {
      leaf_pem_, "-----BEGIN CERTIFICATE-----\nintermediate1\n-----END CERTIFICATE-----",
      "-----BEGIN CERTIFICATE-----\nrootca\n-----END CERTIFICATE-----"};

  EXPECT_CALL(*ssl_, pemEncodedPeerCertificate()).WillOnce(ReturnRef(leaf_pem_));
  EXPECT_CALL(*ssl_, pemEncodedValidatedPeerCertificateChain())
      .WillOnce(Return(absl::MakeConstSpan(validated_chain)));

  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);

  EXPECT_EQ(headers.get_("client-cert"), ":YmFzZTY0X2xlYWZfY2VydA==:");
  EXPECT_EQ(headers.get_("client-cert-chain"), ":intermediate1:, :rootca:");
  EXPECT_EQ(headers.get_("user-agent"), "curl");
}

TEST_F(ClientCertFilterTest, NonTlsConnectionStripsSpoofedHeaders) {
  Http::TestRequestHeaderMapImpl headers{{"client-cert", "evil_spoofed_leaf"},
                                         {"client-cert-chain", "evil_spoofed_chain"},
                                         {"user-agent", "curl"}};

  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));

  EXPECT_CALL(*ssl_, pemEncodedPeerCertificate()).Times(0);
  EXPECT_CALL(*ssl_, pemEncodedValidatedPeerCertificateChain()).Times(0);

  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);

  EXPECT_TRUE(headers.get_("client-cert").empty());
  EXPECT_TRUE(headers.get_("client-cert-chain").empty());
  EXPECT_EQ(headers.get_("user-agent"), "curl");
}

TEST_F(ClientCertFilterTest, NoConnectionStripsSpoofedHeaders) {
  Http::TestRequestHeaderMapImpl headers{{"client-cert", "evil_spoofed_leaf"},
                                         {"client-cert-chain", "evil_spoofed_chain"}};

  ON_CALL(callbacks_, connection()).WillByDefault(Return(OptRef<const Network::Connection>()));

  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);

  EXPECT_TRUE(headers.get_("client-cert").empty());
  EXPECT_TRUE(headers.get_("client-cert-chain").empty());
}

TEST_F(ClientCertFilterTest, EmptyLeafCertificateStripsHeadersAndDoesNotReadd) {
  Http::TestRequestHeaderMapImpl headers{{"client-cert", "evil_spoofed_leaf"},
                                         {"client-cert-chain", "evil_spoofed_chain"}};

  EXPECT_CALL(*ssl_, pemEncodedPeerCertificate()).WillOnce(ReturnRef(empty_pem_));
  // RFC 9440: Client-Cert-Chain must not appear unless Client-Cert is present.
  EXPECT_CALL(*ssl_, pemEncodedValidatedPeerCertificateChain()).Times(0);

  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);

  EXPECT_TRUE(headers.get_("client-cert").empty());
  EXPECT_TRUE(headers.get_("client-cert-chain").empty());
}

TEST_F(ClientCertFilterTest, LeafOnlyValidatedChainOmitsChainHeader) {
  Http::TestRequestHeaderMapImpl headers{{"client-cert-chain", "evil_spoofed_chain"}};

  const std::vector<std::string> validated_chain = {leaf_pem_};

  EXPECT_CALL(*ssl_, pemEncodedPeerCertificate()).WillOnce(ReturnRef(leaf_pem_));
  EXPECT_CALL(*ssl_, pemEncodedValidatedPeerCertificateChain())
      .WillOnce(Return(absl::MakeConstSpan(validated_chain)));

  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);

  EXPECT_EQ(headers.get_("client-cert"), ":YmFzZTY0X2xlYWZfY2VydA==:");
  EXPECT_TRUE(headers.get_("client-cert-chain").empty());
}

TEST_F(ClientCertFilterTest, EmptyValidatedChainOmitsChainHeader) {
  Http::TestRequestHeaderMapImpl headers{};

  EXPECT_CALL(*ssl_, pemEncodedPeerCertificate()).WillOnce(ReturnRef(leaf_pem_));
  EXPECT_CALL(*ssl_, pemEncodedValidatedPeerCertificateChain())
      .WillOnce(Return(absl::Span<const std::string>()));

  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);

  EXPECT_EQ(headers.get_("client-cert"), ":YmFzZTY0X2xlYWZfY2VydA==:");
  EXPECT_TRUE(headers.get_("client-cert-chain").empty());
}

TEST_F(ClientCertFilterTest, ChainDisabledByConfigOmitsChainHeader) {
  Http::TestRequestHeaderMapImpl headers{{"user-agent", "curl"}};

  ClientCertFilter filter = makeFilter(false);

  EXPECT_CALL(*ssl_, pemEncodedPeerCertificate()).WillOnce(ReturnRef(leaf_pem_));
  EXPECT_CALL(*ssl_, pemEncodedValidatedPeerCertificateChain()).Times(0);

  EXPECT_EQ(filter.decodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);

  EXPECT_EQ(headers.get_("client-cert"), ":YmFzZTY0X2xlYWZfY2VydA==:");
  EXPECT_TRUE(headers.get_("client-cert-chain").empty());
  EXPECT_EQ(headers.get_("user-agent"), "curl");
}

} // namespace
} // namespace ClientCert
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
