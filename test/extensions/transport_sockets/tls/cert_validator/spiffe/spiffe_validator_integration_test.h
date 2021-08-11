#pragma once

#include <string>

#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/integration/ssl_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Ssl {

class SslSPIFFECertValidatorIntegrationTest
    : public testing::TestWithParam<
          std::tuple<Network::Address::IpVersion,
                     envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol>>,
      public HttpIntegrationTest {
public:
  SslSPIFFECertValidatorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, std::get<0>(GetParam())) {}

  void initialize() override;
  void TearDown() override;

  virtual Network::ClientConnectionPtr
  makeSslClientConnection(const ClientSslTransportOptions& options, bool use_expired);
  void checkVerifyErrorCouter(uint64_t value);

  static std::string ipClientVersionTestParamsToString(
      const ::testing::TestParamInfo<
          std::tuple<Network::Address::IpVersion,
                     envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol>>&
          params) {
    return fmt::format("{}_TLSv1_{}",
                       std::get<0>(params.param) == Network::Address::IpVersion::v4 ? "IPv4"
                                                                                    : "IPv6",
                       std::get<1>(params.param) - 1);
  }

protected:
  bool allow_expired_cert_{};
  envoy::config::core::v3::TypedExtensionConfig* custom_validator_config_{nullptr};
  std::unique_ptr<ContextManager> context_manager_;
  std::vector<envoy::type::matcher::v3::StringMatcher> san_matchers_;
  const envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol tls_version_{
      std::get<1>(GetParam())};
};

} // namespace Ssl
} // namespace Envoy
