#pragma once

#include <string>

#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/utility.h"

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
    return fmt::format("{}_TLSv1_{}", TestUtility::ipVersionToString(std::get<0>(params.param)),
                       std::get<1>(params.param) - 1);
  }

protected:
  void addStringMatcher(envoy::type::matcher::v3::StringMatcher const& matcher);
  bool allow_expired_cert_{};
  envoy::config::core::v3::TypedExtensionConfig* custom_validator_config_{nullptr};
  std::unique_ptr<ContextManager> context_manager_;
  std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher> san_matchers_;
  const envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol tls_version_{
      std::get<1>(GetParam())};
};

} // namespace Ssl
} // namespace Envoy
