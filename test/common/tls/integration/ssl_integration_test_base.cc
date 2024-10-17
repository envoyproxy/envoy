#include "ssl_integration_test_base.h"

namespace Envoy {
namespace Ssl {

void SslIntegrationTestBase::initialize() {
  config_helper_.addSslConfig(ConfigHelper::ServerSslOptions()
                                  .setRsaCert(server_rsa_cert_)
                                  .setRsaCertOcspStaple(server_rsa_cert_ocsp_staple_)
                                  .setEcdsaCert(server_ecdsa_cert_)
                                  .setEcdsaCertName(server_ecdsa_cert_name_)
                                  .setEcdsaCertOcspStaple(server_ecdsa_cert_ocsp_staple_)
                                  .setOcspStapleRequired(ocsp_staple_required_)
                                  .setPreferClientCiphers(prefer_client_ciphers_)
                                  .setTlsV13(server_tlsv1_3_)
                                  .setCurves(server_curves_)
                                  .setCiphers(server_ciphers_)
                                  .setExpectClientEcdsaCert(client_ecdsa_cert_)
                                  .setTlsCertSelector(tls_cert_selector_yaml_)
                                  .setTlsKeyLogFilter(keylog_local_, keylog_remote_,
                                                      keylog_local_negative_,
                                                      keylog_remote_negative_, keylog_path_,
                                                      keylog_multiple_ips_, version_));

  HttpIntegrationTest::initialize();

  context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
      server_factory_context_);

  registerTestServerPorts({"http"});
}

void SslIntegrationTestBase::TearDown() {
  HttpIntegrationTest::cleanupUpstreamAndDownstream();
  codec_client_.reset();
  context_manager_.reset();
}

Network::ClientConnectionPtr
SslIntegrationTestBase::makeSslClientConnection(const ClientSslTransportOptions& options) {
  Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, lookupPort("http"));
  if (debug_with_s_client_) {
    const std::string s_client_cmd = TestEnvironment::substitute(
        "openssl s_client -connect " + address->asString() +
            " -showcerts -debug -msg -CAfile "
            "{{ test_rundir }}/test/config/integration/certs/cacert.pem "
            "-servername lyft.com -cert "
            "{{ test_rundir }}/test/config/integration/certs/clientcert.pem "
            "-key "
            "{{ test_rundir }}/test/config/integration/certs/clientkey.pem ",
        version_);
    ENVOY_LOG_MISC(debug, "Executing {}", s_client_cmd);
    RELEASE_ASSERT(::system(s_client_cmd.c_str()) == 0, "");
  }
  auto client_transport_socket_factory_ptr =
      createClientSslTransportSocketFactory(options, *context_manager_, *api_);
  return dispatcher_->createClientConnection(
      address, Network::Address::InstanceConstSharedPtr(),
      client_transport_socket_factory_ptr->createTransportSocket({}, nullptr), nullptr, nullptr);
}

void SslIntegrationTestBase::checkStats() {
  const uint32_t expected_handshakes = debug_with_s_client_ ? 2 : 1;
  Stats::CounterSharedPtr counter = test_server_->counter(listenerStatPrefix("ssl.handshake"));
  EXPECT_EQ(expected_handshakes, counter->value());
  counter->reset();
}

} // namespace Ssl
} // namespace Envoy
