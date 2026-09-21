#include <memory>
#include <string>
#include <vector>

#include "source/common/tls/context_manager_impl.h"

#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ClientCert {
namespace {

// Converts each PEM certificate in a bundle to its RFC 9440 byte sequence representation
// (base64 DER wrapped in colons).
std::vector<std::string> pemBundleToRfc9440ByteSequences(const std::string& pem_bundle) {
  constexpr absl::string_view begin_marker = "-----BEGIN CERTIFICATE-----";
  std::vector<std::string> result;
  for (const absl::string_view part : absl::StrSplit(pem_bundle, "-----END CERTIFICATE-----")) {
    const size_t begin = part.find(begin_marker);
    if (begin == absl::string_view::npos) {
      continue;
    }
    std::string body(part.substr(begin + begin_marker.size()));
    absl::StrReplaceAll({{"\n", ""}, {"\r", ""}}, &body);
    result.push_back(absl::StrCat(":", body, ":"));
  }
  return result;
}

class ClientCertIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public HttpIntegrationTest {
public:
  ClientCertIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeFilter(bool set_client_cert_chain, bool tls) {
    config_helper_.prependFilter(absl::StrCat(R"EOF(
name: envoy.filters.http.client_cert
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.client_cert.v3alpha.ClientCertConfig
  set_client_cert_chain: )EOF",
                                              set_client_cert_chain ? "true" : "false"));
    if (tls) {
      // The client presents client2_chain.pem (leaf + intermediate), so the validated
      // chain contains more than just the leaf certificate.
      config_helper_.addSslConfig(
          ConfigHelper::ServerSslOptions().setClientWithIntermediateCert(true));
    }
    HttpIntegrationTest::initialize();
  }

  Network::ClientConnectionPtr makeMtlsClientConnection() {
    if (!ssl_context_manager_) {
      ssl_context_manager_ =
          std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
              server_factory_context_);
    }
    const Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("http"));
    auto client_transport_socket_factory = Ssl::createClientSslTransportSocketFactory(
        Ssl::ClientSslTransportOptions().setClientWithIntermediateCert(true), *ssl_context_manager_,
        *api_, &server_factory_context_.serverScope());
    return dispatcher_->createClientConnection(
        address, Network::Address::InstanceConstSharedPtr(),
        client_transport_socket_factory->createTransportSocket({}, nullptr), nullptr, nullptr);
  }

  // Sends a request with spoofed RFC 9440 headers and returns once the upstream has
  // received it, so the tests can assert on the forwarded headers.
  void sendRequestWithSpoofedHeaders(Network::ClientConnectionPtr&& conn) {
    codec_client_ = makeHttpConnection(std::move(conn));
    auto response = codec_client_->makeHeaderOnlyRequest(
        Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                       {":path", "/"},
                                       {":scheme", "http"},
                                       {":authority", "host"},
                                       {"client-cert", "spoofed-cert"},
                                       {"client-cert-chain", "spoofed-chain"}});
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
  }

  std::string upstreamHeader(absl::string_view name) {
    const auto result = upstream_request_->headers().get(Http::LowerCaseString(name));
    if (result.empty()) {
      return "";
    }
    return std::string(result[0]->value().getStringView());
  }

  std::unique_ptr<Ssl::ContextManager> ssl_context_manager_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClientCertIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ClientCertIntegrationTest, MtlsSetsClientCertAndChain) {
  initializeFilter(/*set_client_cert_chain=*/true, /*tls=*/true);

  sendRequestWithSpoofedHeaders(makeMtlsClientConnection());

  const std::vector<std::string> client_chain =
      pemBundleToRfc9440ByteSequences(TestEnvironment::readFileToStringForTest(
          TestEnvironment::runfilesPath("test/config/integration/certs/client2_chain.pem")));
  ASSERT_GE(client_chain.size(), 2);
  const std::string& leaf = client_chain[0];
  const std::string& intermediate = client_chain[1];

  EXPECT_EQ(upstreamHeader("client-cert"), leaf);

  const std::string chain_header = upstreamHeader("client-cert-chain");
  EXPECT_FALSE(chain_header.empty());
  EXPECT_TRUE(absl::StrContains(chain_header, intermediate));
  // The end-entity certificate must not be repeated in Client-Cert-Chain.
  EXPECT_FALSE(absl::StrContains(chain_header, leaf));
}

TEST_P(ClientCertIntegrationTest, MtlsWithoutChainFlagOmitsChainHeader) {
  initializeFilter(/*set_client_cert_chain=*/false, /*tls=*/true);

  sendRequestWithSpoofedHeaders(makeMtlsClientConnection());

  const std::vector<std::string> client_chain =
      pemBundleToRfc9440ByteSequences(TestEnvironment::readFileToStringForTest(
          TestEnvironment::runfilesPath("test/config/integration/certs/client2_chain.pem")));
  ASSERT_GE(client_chain.size(), 1);

  EXPECT_EQ(upstreamHeader("client-cert"), client_chain[0]);
  EXPECT_TRUE(upstream_request_->headers().get(Http::LowerCaseString("client-cert-chain")).empty());
}

TEST_P(ClientCertIntegrationTest, PlaintextStripsSpoofedHeaders) {
  initializeFilter(/*set_client_cert_chain=*/true, /*tls=*/false);

  sendRequestWithSpoofedHeaders(makeClientConnection(lookupPort("http")));

  EXPECT_TRUE(upstream_request_->headers().get(Http::LowerCaseString("client-cert")).empty());
  EXPECT_TRUE(upstream_request_->headers().get(Http::LowerCaseString("client-cert-chain")).empty());
}

} // namespace
} // namespace ClientCert
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
