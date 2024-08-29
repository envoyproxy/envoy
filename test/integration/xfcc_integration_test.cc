#include "xfcc_integration_test.h"

#include <memory>
#include <regex>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/event/dispatcher_impl.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/network/utility.h"
#include "source/common/tls/client_ssl_socket.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"

#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/container/node_hash_map.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "integration.h"
#include "utility.h"

namespace Envoy {
namespace Xfcc {

void XfccIntegrationTest::TearDown() {
  test_server_.reset();
  client_mtls_ssl_ctx_.reset();
  client_tls_ssl_ctx_.reset();
  fake_upstream_connection_.reset();
  fake_upstreams_.clear();
  HttpIntegrationTest::cleanupUpstreamAndDownstream();
  codec_client_.reset();
  context_manager_.reset();
}

Network::UpstreamTransportSocketFactoryPtr XfccIntegrationTest::createClientSslContext(bool mtls) {
  const std::string yaml_tls = R"EOF(
common_tls_context:
  validation_context:
    trusted_ca:
      filename: {{ test_rundir }}/test/config/integration/certs/cacert.pem
    match_typed_subject_alt_names:
    - san_type: URI
      matcher:
        exact: "spiffe://lyft.com/backend-team"
    - san_type: URI
      matcher:
        exact: "http://backend.lyft.com"
    - san_type: DNS
      matcher:
        exact: "lyft.com"
    - san_type: DNS
      matcher:
        exact: "www.lyft.com"
)EOF";

  const std::string yaml_mtls = R"EOF(
common_tls_context:
  validation_context:
    trusted_ca:
      filename: {{ test_rundir }}/test/config/integration/certs/cacert.pem
    match_typed_subject_alt_names:
    - san_type: URI
      matcher:
        exact: "spiffe://lyft.com/backend-team"
    - san_type: URI
      matcher:
        exact: "http://backend.lyft.com"
    - san_type: DNS
      matcher:
        exact: "lyft.com"
    - san_type: DNS
      matcher:
       exact: "www.lyft.com"
  tls_certificates:
    certificate_chain:
      filename: {{ test_rundir }}/test/config/integration/certs/clientcert.pem
    private_key:
      filename: {{ test_rundir }}/test/config/integration/certs/clientkey.pem
)EOF";

  std::string target;
  if (mtls) {
    target = yaml_mtls;
  } else {
    target = yaml_tls;
  }
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext config;
  TestUtility::loadFromYaml(TestEnvironment::substitute(target), config);
  auto cfg =
      *Extensions::TransportSockets::Tls::ClientContextConfigImpl::create(config, factory_context_);
  static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  return Network::UpstreamTransportSocketFactoryPtr{
      *Extensions::TransportSockets::Tls::ClientSslSocketFactory::create(
          std::move(cfg), *context_manager_, *client_stats_store->rootScope())};
}

Network::DownstreamTransportSocketFactoryPtr XfccIntegrationTest::createUpstreamSslContext() {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  auto* common_tls_context = tls_context.mutable_common_tls_context();
  auto* tls_cert = common_tls_context->add_tls_certificates();
  tls_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
  tls_cert->mutable_private_key()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));

  auto cfg = *Extensions::TransportSockets::Tls::ServerContextConfigImpl::create(
      tls_context, factory_context_, false);
  static auto* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
  return *Extensions::TransportSockets::Tls::ServerSslSocketFactory::create(
      std::move(cfg), *context_manager_, *(upstream_stats_store->rootScope()),
      std::vector<std::string>{});
}

Network::ClientConnectionPtr XfccIntegrationTest::makeTcpClientConnection() {
  Network::Address::InstanceConstSharedPtr address = *Network::Utility::resolveUrl(
      "tcp://" + Network::Test::getLoopbackAddressUrlString(version_) + ":" +
      std::to_string(lookupPort("http")));
  return dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                             Network::Test::createRawBufferSocket(), nullptr,
                                             nullptr);
}

Network::ClientConnectionPtr XfccIntegrationTest::makeMtlsClientConnection() {
  Network::Address::InstanceConstSharedPtr address = *Network::Utility::resolveUrl(
      "tcp://" + Network::Test::getLoopbackAddressUrlString(version_) + ":" +
      std::to_string(lookupPort("http")));
  return dispatcher_->createClientConnection(
      address, Network::Address::InstanceConstSharedPtr(),
      client_mtls_ssl_ctx_->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
}

void XfccIntegrationTest::createUpstreams() {
  addFakeUpstream(createUpstreamSslContext(), Http::CodecType::HTTP1,
                  /*autonomous_upstream=*/false);
}

void XfccIntegrationTest::initialize() {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.set_forward_client_cert_details(fcc_);
        hcm.mutable_set_current_client_cert_details()->CopyFrom(sccd_);
      });

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto transport_socket =
        bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext context;
    auto* validation_context = context.mutable_common_tls_context()->mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
    auto* san_matcher = validation_context->add_match_typed_subject_alt_names();
    san_matcher->mutable_matcher()->set_suffix("lyft.com");
    san_matcher->set_san_type(
        envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS);
    transport_socket->set_name("envoy.transport_sockets.tls");
    transport_socket->mutable_typed_config()->PackFrom(context);
  });

  if (tls_) {
    config_helper_.addSslConfig();
  }

  context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
      server_factory_context_);
  client_tls_ssl_ctx_ = createClientSslContext(false);
  client_mtls_ssl_ctx_ = createClientSslContext(true);
  HttpIntegrationTest::initialize();
}

void XfccIntegrationTest::testRequestAndResponseWithXfccHeader(std::string previous_xfcc,
                                                               std::string expected_xfcc) {
  Network::ClientConnectionPtr conn = tls_ ? makeMtlsClientConnection() : makeTcpClientConnection();
  Http::TestRequestHeaderMapImpl header_map;
  if (previous_xfcc.empty()) {
    header_map = Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                {":path", "/test/long/url"},
                                                {":scheme", "http"},
                                                {":authority", "host"}};
  } else {
    header_map = Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                {":path", "/test/long/url"},
                                                {":scheme", "http"},
                                                {":authority", "host"},
                                                {"x-forwarded-client-cert", previous_xfcc.c_str()}};
  }

  codec_client_ = makeHttpConnection(std::move(conn));
  auto response = codec_client_->makeHeaderOnlyRequest(header_map);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  if (expected_xfcc.empty()) {
    EXPECT_EQ(nullptr, upstream_request_->headers().ForwardedClientCert());
  } else {
    EXPECT_EQ(expected_xfcc, upstream_request_->headers().getForwardedClientCertValue());
  }
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
}

INSTANTIATE_TEST_SUITE_P(IpVersions, XfccIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(XfccIntegrationTest, MtlsForwardOnly) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      FORWARD_ONLY;
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_, previous_xfcc_);
}

TEST_P(XfccIntegrationTest, MtlsAlwaysForwardOnly) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      ALWAYS_FORWARD_ONLY;
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_, previous_xfcc_);
}

TEST_P(XfccIntegrationTest, MtlsSanitize) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      SANITIZE;
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_, "");
}

TEST_P(XfccIntegrationTest, MtlsSanitizeSetSubject) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      SANITIZE_SET;
  sccd_.mutable_subject()->set_value(true);
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_,
                                       current_xfcc_by_hash_ + ";" + client_subject_);
}

TEST_P(XfccIntegrationTest, MtlsSanitizeSetUri) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      SANITIZE_SET;
  sccd_.set_uri(true);
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_,
                                       current_xfcc_by_hash_ + ";" + client_uri_san_);
}

TEST_P(XfccIntegrationTest, MtlsSanitizeSetDns) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      SANITIZE_SET;
  sccd_.set_dns(true);
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_,
                                       current_xfcc_by_hash_ + ";" + client_dns_san_);
}

TEST_P(XfccIntegrationTest, MtlsSanitizeSetSubjectUri) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      SANITIZE_SET;
  sccd_.mutable_subject()->set_value(true);
  sccd_.set_uri(true);
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_, current_xfcc_by_hash_ + ";" +
                                                           client_subject_ + ";" + client_uri_san_);
}

TEST_P(XfccIntegrationTest, MtlsSanitizeSetSubjectDns) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      SANITIZE_SET;
  sccd_.mutable_subject()->set_value(true);
  sccd_.set_dns(true);
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_, current_xfcc_by_hash_ + ";" +
                                                           client_subject_ + ";" + client_dns_san_);
}

TEST_P(XfccIntegrationTest, MtlsSanitizeSetSubjectUriDns) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      SANITIZE_SET;
  sccd_.mutable_subject()->set_value(true);
  sccd_.set_uri(true);
  sccd_.set_dns(true);
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_, current_xfcc_by_hash_ + ";" +
                                                           client_subject_ + ";" + client_uri_san_ +
                                                           ";" + client_dns_san_);
}

TEST_P(XfccIntegrationTest, MtlsAppendForward) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      APPEND_FORWARD;
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_,
                                       previous_xfcc_ + "," + current_xfcc_by_hash_);
}

TEST_P(XfccIntegrationTest, MtlsAppendForwardSubject) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      APPEND_FORWARD;
  sccd_.mutable_subject()->set_value(true);
  initialize();
  testRequestAndResponseWithXfccHeader(
      previous_xfcc_, previous_xfcc_ + "," + current_xfcc_by_hash_ + ";" + client_subject_);
}

TEST_P(XfccIntegrationTest, MtlsAppendForwardUri) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      APPEND_FORWARD;
  sccd_.set_uri(true);
  initialize();
  testRequestAndResponseWithXfccHeader(
      previous_xfcc_, previous_xfcc_ + "," + current_xfcc_by_hash_ + ";" + client_uri_san_);
}

TEST_P(XfccIntegrationTest, MtlsAppendForwardDns) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      APPEND_FORWARD;
  sccd_.set_dns(true);
  initialize();
  testRequestAndResponseWithXfccHeader(
      previous_xfcc_, previous_xfcc_ + "," + current_xfcc_by_hash_ + ";" + client_dns_san_);
}

TEST_P(XfccIntegrationTest, MtlsAppendForwardSubjectUri) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      APPEND_FORWARD;
  sccd_.mutable_subject()->set_value(true);
  sccd_.set_uri(true);
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_, previous_xfcc_ + "," +
                                                           current_xfcc_by_hash_ + ";" +
                                                           client_subject_ + ";" + client_uri_san_);
}

TEST_P(XfccIntegrationTest, MtlsAppendForwardSubjectDns) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      APPEND_FORWARD;
  sccd_.mutable_subject()->set_value(true);
  sccd_.set_dns(true);
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_, previous_xfcc_ + "," +
                                                           current_xfcc_by_hash_ + ";" +
                                                           client_subject_ + ";" + client_dns_san_);
}

TEST_P(XfccIntegrationTest, MtlsAppendForwardSubjectUriDns) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      APPEND_FORWARD;
  sccd_.mutable_subject()->set_value(true);
  sccd_.set_uri(true);
  sccd_.set_dns(true);
  initialize();
  testRequestAndResponseWithXfccHeader(
      previous_xfcc_, previous_xfcc_ + "," + current_xfcc_by_hash_ + ";" + client_subject_ + ";" +
                          client_uri_san_ + ";" + client_dns_san_);
}

TEST_P(XfccIntegrationTest, MtlsAppendForwardUriPreviousXfccHeaderEmpty) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      APPEND_FORWARD;
  sccd_.set_uri(true);
  initialize();
  testRequestAndResponseWithXfccHeader("", current_xfcc_by_hash_ + ";" + client_uri_san_);
}

TEST_P(XfccIntegrationTest, MtlsAppendForwardDnsPreviousXfccHeaderEmpty) {
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      APPEND_FORWARD;
  sccd_.set_dns(true);
  initialize();
  testRequestAndResponseWithXfccHeader("", current_xfcc_by_hash_ + ";" + client_dns_san_);
}

TEST_P(XfccIntegrationTest, TlsAlwaysForwardOnly) {
  // The always_forward_only works regardless of whether the connection is TLS/mTLS.
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      ALWAYS_FORWARD_ONLY;
  tls_ = false;
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_, previous_xfcc_);
}

TEST_P(XfccIntegrationTest, TlsEnforceSanitize) {
  // The forward_only, append_forward and sanitize_set options are not effective when the connection
  // is not using Mtls.
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      FORWARD_ONLY;
  tls_ = false;
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_, "");
}

TEST_P(XfccIntegrationTest, NonTlsAlwaysForwardOnly) {
  // The always_forward_only works regardless of whether the connection is TLS/mTLS.
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      ALWAYS_FORWARD_ONLY;
  tls_ = false;
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_, previous_xfcc_);
}

TEST_P(XfccIntegrationTest, NonTlsEnforceSanitize) {
  // The forward_only, append_forward and sanitize_set options are not effective when the connection
  // is not using Mtls.
  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      FORWARD_ONLY;
  tls_ = false;
  initialize();
  testRequestAndResponseWithXfccHeader(previous_xfcc_, "");
}

TEST_P(XfccIntegrationTest, TagExtractedNameGenerationTest) {
  // Note: the test below is meant to check that default tags are being extracted correctly with
  // real-ish input stats. If new stats are added, this test will not break because names that do
  // not exist in the map are not checked. However, if stats are modified the below maps should be
  // updated (or regenerated by printing in map literal format). See commented code below to
  // regenerate the maps. Note: different maps are needed for ipv4 and ipv6, so when regenerating,
  // the printout needs to be copied from each test parameterization and pasted into the respective
  // case in the switch statement below.

  fcc_ = envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      FORWARD_ONLY;
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto listener = config_helper_.buildBaseListener(
        "with_stat_prefix", Network::Test::getLoopbackAddressString(version_));
    listener.set_stat_prefix("my_prefix");
    *listener.mutable_filter_chains() = bootstrap.static_resources().listeners(0).filter_chains();
    *bootstrap.mutable_static_resources()->add_listeners() = listener;
  });
  initialize();

  // Make sure worker threads are established (#32237).
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      test_server_->adminAddress(), "GET", "/config_dump", "", Http::CodecType::HTTP1);
  EXPECT_TRUE(response->complete());

  // Commented sample code to regenerate the map literals used below in the test log if necessary:

  // std::cout << "tag_extracted_counter_map = {";
  // std::vector<Stats::CounterSharedPtr> counters = test_server_->counters();
  // for (auto it = counters.begin(); it != counters.end(); ++it) {
  //   if (it != counters.begin()) {
  //     std::cout << ",";
  //   }
  //   std::cout << std::endl << "{\"" << (*it)->name() << "\", \"" << (*it)->tagExtractedName() <<
  //   "\"}";
  // }
  // std::cout << "};" << std::endl;
  // std::cout << "tag_extracted_gauge_map = {";
  // std::vector<Stats::GaugeSharedPtr> gauges = test_server_->gauges();
  // for (auto it = gauges.begin(); it != gauges.end(); ++it) {
  //   if (it != gauges.begin()) {
  //     std::cout << ",";
  //   }
  //   std::cout << std::endl << "{\"" << (*it)->name() << "\", \"" << (*it)->tagExtractedName() <<
  //   "\"}";
  // }
  // std::cout << "};" << std::endl;

  using MetricMap = absl::node_hash_map<std::string, std::string>;

  MetricMap tag_extracted_counter_map;
  MetricMap tag_extracted_gauge_map;
  tag_extracted_counter_map = {
      {listenerStatPrefix("downstream_cx_overflow"), "listener.downstream_cx_overflow"},
      {listenerStatPrefix("downstream_cx_overload_reject"),
       "listener.downstream_cx_overload_reject"},
      {listenerStatPrefix("server_ssl_socket_factory.downstream_context_secrets_not_ready"),
       "listener.server_ssl_socket_factory.downstream_context_secrets_not_ready"},
      {listenerStatPrefix("downstream_cx_total"), "listener.downstream_cx_total"},
      {"listener.my_prefix.downstream_cx_total", "listener.downstream_cx_total"},
      {listenerStatPrefix("downstream_cx_destroy"), "listener.downstream_cx_destroy"},
      {listenerStatPrefix("ssl.ocsp_staple_requests"), "listener.ssl.ocsp_staple_requests"},
      {listenerStatPrefix("ssl.handshake"), "listener.ssl.handshake"},
      {listenerStatPrefix("server_ssl_socket_factory.upstream_context_secrets_not_ready"),
       "listener.server_ssl_socket_factory.upstream_context_secrets_not_ready"},
      {listenerStatPrefix("ssl.fail_verify_error"), "listener.ssl.fail_verify_error"},
      {listenerStatPrefix("http.config_test.downstream_rq_4xx"), "listener.http.downstream_rq_xx"},
      {listenerStatPrefix("ssl.connection_error"), "listener.ssl.connection_error"},
      {listenerStatPrefix("http.config_test.downstream_rq_completed"),
       "listener.http.downstream_rq_completed"},
      {listenerStatPrefix("no_filter_chain_match"), "listener.no_filter_chain_match"},
      {listenerStatPrefix("ssl.ocsp_staple_omitted"), "listener.ssl.ocsp_staple_omitted"},
      {listenerStatPrefix("ssl.no_certificate"), "listener.ssl.no_certificate"},
      {listenerStatPrefix("ssl.ocsp_staple_failed"), "listener.ssl.ocsp_staple_failed"},
      {listenerStatPrefix("http.config_test.downstream_rq_2xx"), "listener.http.downstream_rq_xx"},
      {listenerStatPrefix("http.config_test.downstream_rq_5xx"), "listener.http.downstream_rq_xx"},
      {listenerStatPrefix("worker_0.downstream_cx_total"), "listener.worker_downstream_cx_total"},
      {listenerStatPrefix("ssl.fail_verify_san"), "listener.ssl.fail_verify_san"},
      {listenerStatPrefix("downstream_cx_transport_socket_connect_timeout"),
       "listener.downstream_cx_transport_socket_connect_timeout"},
      {listenerStatPrefix("downstream_pre_cx_timeout"), "listener.downstream_pre_cx_timeout"},
      {listenerStatPrefix("ssl.ocsp_staple_responses"), "listener.ssl.ocsp_staple_responses"},
      {listenerStatPrefix("ssl.fail_verify_no_cert"), "listener.ssl.fail_verify_no_cert"},
      {listenerStatPrefix("server_ssl_socket_factory.ssl_context_update_by_sds"),
       "listener.server_ssl_socket_factory.ssl_context_update_by_sds"},
      {listenerStatPrefix("downstream_global_cx_overflow"),
       "listener.downstream_global_cx_overflow"},
      {listenerStatPrefix("http.config_test.downstream_rq_3xx"), "listener.http.downstream_rq_xx"},
      {listenerStatPrefix("http.config_test.downstream_rq_1xx"), "listener.http.downstream_rq_xx"},
      {listenerStatPrefix("ssl.session_reused"), "listener.ssl.session_reused"},
      {listenerStatPrefix("ssl.fail_verify_cert_hash"), "listener.ssl.fail_verify_cert_hash"},
      {"listener_manager.listener_added", "listener_manager.listener_added"},
      {"cluster.cluster_0.upstream_flow_control_resumed_reading_total",
       "cluster.upstream_flow_control_resumed_reading_total"},
      {"cluster.cluster_0.lb_local_cluster_not_ok", "cluster.lb_local_cluster_not_ok"},
      {"runtime.override_dir_not_exists", "runtime.override_dir_not_exists"},
      {"cluster.cluster_0.ssl.fail_verify_san", "cluster.ssl.fail_verify_san"},
      {"http.config_test.downstream_rq_too_large", "http.downstream_rq_too_large"},
      {"listener_manager.listener_create_failure", "listener_manager.listener_create_failure"},
      {"cluster.cluster_0.upstream_rq_pending_overflow", "cluster.upstream_rq_pending_overflow"},
      {"http.config_test.downstream_rq_1xx", "http.downstream_rq_xx"},
      {"http.admin.downstream_rq_overload_close", "http.downstream_rq_overload_close"},
      {"main_thread.watchdog_mega_miss", "main_thread.watchdog_mega_miss"},
      {"listener_manager.lds.update_attempt", "listener_manager.lds.update_attempt"},
      {"cluster.cluster_0.upstream_rq_rx_reset", "cluster.upstream_rq_rx_reset"},
      {"listener_manager.listener_modified", "listener_manager.listener_modified"},
      {"http.config_test.passthrough_internal_redirect_unsafe_scheme",
       "http.passthrough_internal_redirect_unsafe_scheme"},
      {"cluster.cluster_0.upstream_rq_per_try_idle_timeout",
       "cluster.upstream_rq_per_try_idle_timeout"},
      {"listener_manager.listener_stopped", "listener_manager.listener_stopped"},
      {"http.admin.downstream_rq_failed_path_normalization",
       "http.downstream_rq_failed_path_normalization"},
      {"http.config_test.passthrough_internal_redirect_no_route",
       "http.passthrough_internal_redirect_no_route"},
      {"cluster.cluster_0.upstream_cx_connect_attempts_exceeded",
       "cluster.upstream_cx_connect_attempts_exceeded"},
      {"cluster.cluster_0.ssl.connection_error", "cluster.ssl.connection_error"},
      {"listener_manager.listener_create_success", "listener_manager.listener_create_success"},
      {"workers.watchdog_miss", "workers.watchdog_miss"},
      {"http.admin.downstream_rq_tx_reset", "http.downstream_rq_tx_reset"},
      {"cluster.cluster_0.update_empty", "cluster.update_empty"},
      {"cluster.cluster_0.upstream_cx_none_healthy", "cluster.upstream_cx_none_healthy"},
      {"http.admin.downstream_rq_max_duration_reached", "http.downstream_rq_max_duration_reached"},
      {"cluster.cluster_0.upstream_cx_http2_total", "cluster.upstream_cx_http2_total"},
      {"listener.admin.http.admin.downstream_rq_4xx", "listener.admin.http.downstream_rq_xx"},
      {"cluster.cluster_0.upstream_cx_connect_timeout", "cluster.upstream_cx_connect_timeout"},
      {"cluster.cluster_0.upstream_rq_per_try_timeout", "cluster.upstream_rq_per_try_timeout"},
      {"cluster.cluster_0.lb_recalculate_zone_structures",
       "cluster.lb_recalculate_zone_structures"},
      {"cluster.cluster_0.lb_healthy_panic", "cluster.lb_healthy_panic"},
      {"http.config_test.no_route", "http.no_route"},
      {"http.admin.downstream_rq_rejected_via_ip_detection",
       "http.downstream_rq_rejected_via_ip_detection"},
      {"filesystem.flushed_by_timer", "filesystem.flushed_by_timer"},
      {"cluster.cluster_0.client_ssl_socket_factory.downstream_context_secrets_not_ready",
       "cluster.client_ssl_socket_factory.downstream_context_secrets_not_ready"},
      {"http.admin.downstream_rq_5xx", "http.downstream_rq_xx"},
      {"http.admin.downstream_rq_completed", "http.downstream_rq_completed"},
      {"http.config_test.downstream_cx_tx_bytes_total", "http.downstream_cx_tx_bytes_total"},
      {"http.config_test.downstream_rq_http3_total", "http.downstream_rq_http3_total"},
      {"http.admin.downstream_cx_http2_total", "http.downstream_cx_http2_total"},
      {"cluster.cluster_0.upstream_rq_retry_overflow", "cluster.upstream_rq_retry_overflow"},
      {"runtime.deprecated_feature_use", "runtime.deprecated_feature_use"},
      {"listener_manager.lds.update_failure", "listener_manager.lds.update_failure"},
      {"http.admin.downstream_rq_idle_timeout", "http.downstream_rq_idle_timeout"},
      {"cluster.cluster_0.update_no_rebuild", "cluster.update_no_rebuild"},
      {"http.admin.downstream_rq_http1_total", "http.downstream_rq_http1_total"},
      {"cluster.cluster_0.upstream_cx_rx_bytes_total", "cluster.upstream_cx_rx_bytes_total"},
      {"http.admin.downstream_flow_control_paused_reading_total",
       "http.downstream_flow_control_paused_reading_total"},
      {"cluster_manager.update_merge_cancelled", "cluster_manager.update_merge_cancelled"},
      {"cluster_manager.cluster_updated", "cluster_manager.cluster_updated"},
      {"http.config_test.downstream_rq_total", "http.downstream_rq_total"},
      {"http.config_test.tracing.health_check", "http.tracing.health_check"},
      {"http.admin.downstream_cx_destroy_remote", "http.downstream_cx_destroy_remote"},
      {"cluster.cluster_0.upstream_rq_retry_backoff_ratelimited",
       "cluster.upstream_rq_retry_backoff_ratelimited"},
      {"cluster.cluster_0.ssl.fail_verify_cert_hash", "cluster.ssl.fail_verify_cert_hash"},
      {"http.config_test.no_cluster", "http.no_cluster"},
      {"http.admin.downstream_rq_header_timeout", "http.downstream_rq_header_timeout"},
      {"http.config_test.downstream_rq_5xx", "http.downstream_rq_xx"},
      {"http.admin.downstream_cx_rx_bytes_total", "http.downstream_cx_rx_bytes_total"},
      {"http.config_test.downstream_cx_destroy_active_rq", "http.downstream_cx_destroy_active_rq"},
      {"http.admin.downstream_flow_control_resumed_reading_total",
       "http.downstream_flow_control_resumed_reading_total"},
      {"http.config_test.rq_total", "http.rq_total"},
      {"http.admin.downstream_rq_ws_on_non_ws_route", "http.downstream_rq_ws_on_non_ws_route"},
      {"server.main_thread.watchdog_miss", "server.main_thread.watchdog_miss"},
      {"http.config_test.downstream_cx_total", "http.downstream_cx_total"},
      {"http.config_test.downstream_rq_http2_total", "http.downstream_rq_http2_total"},
      {"cluster.cluster_0.upstream_cx_http1_total", "cluster.upstream_cx_http1_total"},
      {"http.config_test.rq_redirect", "http.rq_redirect"},
      {"http.admin.downstream_cx_http3_total", "http.downstream_cx_http3_total"},
      {"listener.admin.http.admin.downstream_rq_2xx", "listener.admin.http.downstream_rq_xx"},
      {"http.config_test.downstream_cx_destroy_local", "http.downstream_cx_destroy_local"},
      {"cluster.cluster_0.lb_zone_routing_cross_zone", "cluster.lb_zone_routing_cross_zone"},
      {"http.admin.downstream_cx_destroy_remote_active_rq",
       "http.downstream_cx_destroy_remote_active_rq"},
      {"cluster.cluster_0.ssl.session_reused", "cluster.ssl.session_reused"},
      {"cluster.cluster_0.ssl.ocsp_staple_failed", "cluster.ssl.ocsp_staple_failed"},
      {"cluster.cluster_0.ssl.ocsp_staple_omitted", "cluster.ssl.ocsp_staple_omitted"},
      {"cluster.cluster_0.update_success", "cluster.update_success"},
      {"http.admin.downstream_rq_non_relative_path", "http.downstream_rq_non_relative_path"},
      {"cluster.cluster_0.lb_zone_number_differs", "cluster.lb_zone_number_differs"},
      {"http.admin.downstream_rq_timeout", "http.downstream_rq_timeout"},
      {"cluster.cluster_0.retry_or_shadow_abandoned", "cluster.retry_or_shadow_abandoned"},
      {"http.admin.downstream_cx_ssl_total", "http.downstream_cx_ssl_total"},
      {"server.debug_assertion_failures", "server.debug_assertion_failures"},
      {"listener_manager.lds.update_rejected", "listener_manager.lds.update_rejected"},
      {"cluster.cluster_0.upstream_cx_connect_fail", "cluster.upstream_cx_connect_fail"},
      {"filesystem.reopen_failed", "filesystem.reopen_failed"},
      {"http.config_test.downstream_cx_protocol_error", "http.downstream_cx_protocol_error"},
      {"cluster.cluster_0.upstream_rq_tx_reset", "cluster.upstream_rq_tx_reset"},
      {"server.dropped_stat_flushes", "server.dropped_stat_flushes"},
      {"http.admin.downstream_rq_response_before_rq_complete",
       "http.downstream_rq_response_before_rq_complete"},
      {"http.config_test.tracing.service_forced", "http.tracing.service_forced"},
      {"server.envoy_bug_failures", "server.envoy_bug_failures"},
      {"http.admin.downstream_rq_total", "http.downstream_rq_total"},
      {"http.config_test.downstream_rq_idle_timeout", "http.downstream_rq_idle_timeout"},
      {"cluster.cluster_0.upstream_rq_retry_success", "cluster.upstream_rq_retry_success"},
      {"http.config_test.rq_reset_after_downstream_response_started",
       "http.rq_reset_after_downstream_response_started"},
      {"cluster.cluster_0.upstream_cx_destroy_remote_with_active_rq",
       "cluster.upstream_cx_destroy_remote_with_active_rq"},
      {"envoy.overload_actions.reset_high_memory_stream.count",
       "envoy.overload_actions.reset_high_memory_stream.count"},
      {"http.config_test.downstream_rq_4xx", "http.downstream_rq_xx"},
      {"http.config_test.downstream_rq_ws_on_non_ws_route",
       "http.downstream_rq_ws_on_non_ws_route"},
      {"cluster.cluster_0.update_attempt", "cluster.update_attempt"},
      {"cluster.cluster_0.upstream_rq_retry_limit_exceeded",
       "cluster.upstream_rq_retry_limit_exceeded"},
      {"cluster_manager.cluster_updated_via_merge", "cluster_manager.cluster_updated_via_merge"},
      {"listener.admin.downstream_cx_total", "listener.admin.downstream_cx_total"},
      {"cluster.cluster_0.upstream_rq_pending_failure_eject",
       "cluster.upstream_rq_pending_failure_eject"},
      {"http.config_test.downstream_cx_http3_total", "http.downstream_cx_http3_total"},
      {"cluster.cluster_0.upstream_rq_maintenance_mode", "cluster.upstream_rq_maintenance_mode"},
      {"runtime.load_error", "runtime.load_error"},
      {"http.config_test.downstream_rq_3xx", "http.downstream_rq_xx"},
      {"cluster.cluster_0.upstream_cx_http3_total", "cluster.upstream_cx_http3_total"},
      {"cluster.cluster_0.ssl.ocsp_staple_requests", "cluster.ssl.ocsp_staple_requests"},
      {"server.main_thread.watchdog_mega_miss", "server.main_thread.watchdog_mega_miss"},
      {"http.admin.downstream_cx_drain_close", "http.downstream_cx_drain_close"},
      {"cluster.cluster_0.ssl.no_certificate", "cluster.ssl.no_certificate"},
      {"http.config_test.downstream_cx_upgrades_total", "http.downstream_cx_upgrades_total"},
      {"cluster.cluster_0.upstream_rq_timeout", "cluster.upstream_rq_timeout"},
      {"http.admin.downstream_cx_delayed_close_timeout",
       "http.downstream_cx_delayed_close_timeout"},
      {"http.config_test.downstream_flow_control_resumed_reading_total",
       "http.downstream_flow_control_resumed_reading_total"},
      {"server.wip_protos", "server.wip_protos"},
      {"cluster.cluster_0.upstream_internal_redirect_failed_total",
       "cluster.upstream_internal_redirect_failed_total"},
      {"cluster.cluster_0.lb_subsets_fallback_panic", "cluster.lb_subsets_fallback_panic"},
      {"cluster.cluster_0.upstream_rq_pending_total", "cluster.upstream_rq_pending_total"},
      {"http.config_test.downstream_cx_destroy_remote_active_rq",
       "http.downstream_cx_destroy_remote_active_rq"},
      {"http.admin.downstream_rq_3xx", "http.downstream_rq_xx"},
      {"http.admin.downstream_cx_upgrades_total", "http.downstream_cx_upgrades_total"},
      {"cluster.cluster_0.default.total_match_count", "cluster.default.total_match_count"},
      {"cluster.cluster_0.upstream_internal_redirect_succeeded_total",
       "cluster.upstream_internal_redirect_succeeded_total"},
      {"http.admin.downstream_cx_max_requests_reached", "http.downstream_cx_max_requests_reached"},
      {"http.config_test.downstream_cx_ssl_total", "http.downstream_cx_ssl_total"},
      {"cluster.cluster_0.ssl.ocsp_staple_responses", "cluster.ssl.ocsp_staple_responses"},
      {"http.config_test.downstream_rq_overload_close", "http.downstream_rq_overload_close"},
      {"http.config_test.downstream_rq_non_relative_path", "http.downstream_rq_non_relative_path"},
      {"listener.admin.main_thread.downstream_cx_total",
       "listener.admin.main_thread.downstream_cx_total"},
      {"cluster.cluster_0.upstream_cx_idle_timeout", "cluster.upstream_cx_idle_timeout"},
      {"cluster.cluster_0.upstream_flow_control_drained_total",
       "cluster.upstream_flow_control_drained_total"},
      {"listener_manager.listener_removed", "listener_manager.listener_removed"},
      {"cluster.cluster_0.upstream_rq_completed", "cluster.upstream_rq_completed"},
      {"cluster.cluster_0.bind_errors", "cluster.bind_errors"},
      {"cluster.cluster_0.upstream_rq_max_duration_reached",
       "cluster.upstream_rq_max_duration_reached"},
      {"http.config_test.downstream_cx_destroy", "http.downstream_cx_destroy"},
      {"cluster_manager.cluster_added", "cluster_manager.cluster_added"},
      {"http.config_test.downstream_rq_failed_path_normalization",
       "http.downstream_rq_failed_path_normalization"},
      {"cluster.cluster_0.original_dst_host_invalid", "cluster.original_dst_host_invalid"},
      {"server.worker_0.watchdog_miss", "server.worker_watchdog_miss"},
      {"http.config_test.downstream_rq_rx_reset", "http.downstream_rq_rx_reset"},
      {"cluster.cluster_0.upstream_cx_destroy_local_with_active_rq",
       "cluster.upstream_cx_destroy_local_with_active_rq"},
      {"runtime.load_success", "runtime.load_success"},
      {"filesystem.write_failed", "filesystem.write_failed"},
      {"cluster_manager.cluster_modified", "cluster_manager.cluster_modified"},
      {"filesystem.write_completed", "filesystem.write_completed"},
      {"cluster.cluster_0.upstream_cx_destroy_with_active_rq",
       "cluster.upstream_cx_destroy_with_active_rq"},
      {"cluster.cluster_0.lb_zone_cluster_too_small", "cluster.lb_zone_cluster_too_small"},
      {"cluster.cluster_0.ssl.fail_verify_no_cert", "cluster.ssl.fail_verify_no_cert"},
      {"http.config_test.downstream_rq_timeout", "http.downstream_rq_timeout"},
      {"workers.watchdog_mega_miss", "workers.watchdog_mega_miss"},
      {"http.config_test.downstream_cx_http1_total", "http.downstream_cx_http1_total"},
      {"http.config_test.downstream_cx_rx_bytes_total", "http.downstream_cx_rx_bytes_total"},
      {"http.config_test.downstream_cx_http2_total", "http.downstream_cx_http2_total"},
      {"http.config_test.downstream_rq_http1_total", "http.downstream_rq_http1_total"},
      {"http.config_test.passthrough_internal_redirect_bad_location",
       "http.passthrough_internal_redirect_bad_location"},
      {"cluster.cluster_0.lb_subsets_removed", "cluster.lb_subsets_removed"},
      {"http.admin.downstream_cx_tx_bytes_total", "http.downstream_cx_tx_bytes_total"},
      {"http.config_test.tracing.not_traceable", "http.tracing.not_traceable"},
      {"http.config_test.downstream_cx_destroy_local_active_rq",
       "http.downstream_cx_destroy_local_active_rq"},
      {"cluster.cluster_0.client_ssl_socket_factory.ssl_context_update_by_sds",
       "cluster.client_ssl_socket_factory.ssl_context_update_by_sds"},
      {"http.config_test.downstream_rq_tx_reset", "http.downstream_rq_tx_reset"},
      {"http.admin.downstream_cx_total", "http.downstream_cx_total"},
      {"http.config_test.tracing.client_enabled", "http.tracing.client_enabled"},
      {"http.config_test.downstream_cx_drain_close", "http.downstream_cx_drain_close"},
      {"runtime.override_dir_exists", "runtime.override_dir_exists"},
      {"listener_manager.lds.init_fetch_timeout", "listener_manager.lds.init_fetch_timeout"},
      {"cluster.cluster_0.upstream_rq_total", "cluster.upstream_rq_total"},
      {"http.admin.downstream_rq_too_large", "http.downstream_rq_too_large"},
      {"http.admin.downstream_rq_1xx", "http.downstream_rq_xx"},
      {"http.config_test.rs_too_large", "http.rs_too_large"},
      {"cluster.cluster_0.ssl.fail_verify_error", "cluster.ssl.fail_verify_error"},
      {"http.admin.downstream_cx_destroy_local", "http.downstream_cx_destroy_local"},
      {"http.config_test.downstream_cx_overload_disable_keepalive",
       "http.downstream_cx_overload_disable_keepalive"},
      {"cluster_manager.cluster_removed", "cluster_manager.cluster_removed"},
      {"http.config_test.downstream_rq_max_duration_reached",
       "http.downstream_rq_max_duration_reached"},
      {"http.config_test.downstream_rq_header_timeout", "http.downstream_rq_header_timeout"},
      {"http.admin.downstream_rq_http3_total", "http.downstream_rq_http3_total"},
      {"http.config_test.rq_direct_response", "http.rq_direct_response"},
      {"http.admin.downstream_cx_destroy_local_active_rq",
       "http.downstream_cx_destroy_local_active_rq"},
      {"filesystem.write_buffered", "filesystem.write_buffered"},
      {"cluster.cluster_0.lb_subsets_fallback", "cluster.lb_subsets_fallback"},
      {"listener_manager.listener_in_place_updated", "listener_manager.listener_in_place_updated"},
      {"cluster.cluster_0.upstream_cx_total", "cluster.upstream_cx_total"},
      {"http.admin.downstream_rq_2xx", "http.downstream_rq_xx"},
      {"cluster.cluster_0.lb_subsets_created", "cluster.lb_subsets_created"},
      {"cluster.cluster_0.upstream_rq_retry_backoff_exponential",
       "cluster.upstream_rq_retry_backoff_exponential"},
      {"cluster.cluster_0.update_failure", "cluster.update_failure"},
      {"cluster.cluster_0.upstream_cx_overflow", "cluster.upstream_cx_overflow"},
      {"listener.admin.no_filter_chain_match", "listener.admin.no_filter_chain_match"},
      {"http.config_test.passthrough_internal_redirect_too_many_redirects",
       "http.passthrough_internal_redirect_too_many_redirects"},
      {"http.config_test.downstream_rq_rejected_via_ip_detection",
       "http.downstream_rq_rejected_via_ip_detection"},
      {"cluster.cluster_0.upstream_cx_destroy", "cluster.upstream_cx_destroy"},
      {"http.config_test.downstream_rq_redirected_with_normalized_path",
       "http.downstream_rq_redirected_with_normalized_path"},
      {"cluster.cluster_0.upstream_cx_destroy_local", "cluster.upstream_cx_destroy_local"},
      {"http.admin.downstream_rq_http2_total", "http.downstream_rq_http2_total"},
      {"http.config_test.downstream_cx_max_duration_reached",
       "http.downstream_cx_max_duration_reached"},
      {"listener.admin.downstream_global_cx_overflow",
       "listener.admin.downstream_global_cx_overflow"},
      {"listener.admin.downstream_cx_overload_reject",
       "listener.admin.downstream_cx_overload_reject"},
      {"http.config_test.tracing.random_sampling", "http.tracing.random_sampling"},
      {"cluster.cluster_0.upstream_cx_pool_overflow", "cluster.upstream_cx_pool_overflow"},
      {"server.worker_0.watchdog_mega_miss", "server.worker_watchdog_mega_miss"},
      {"cluster.cluster_0.upstream_cx_tx_bytes_total", "cluster.upstream_cx_tx_bytes_total"},
      {"http.admin.rs_too_large", "http.rs_too_large"},
      {"listener.admin.downstream_cx_transport_socket_connect_timeout",
       "listener.admin.downstream_cx_transport_socket_connect_timeout"},
      {"cluster.cluster_0.lb_subsets_selected", "cluster.lb_subsets_selected"},
      {"http.admin.downstream_cx_overload_disable_keepalive",
       "http.downstream_cx_overload_disable_keepalive"},
      {"cluster.cluster_0.lb_zone_routing_all_directly", "cluster.lb_zone_routing_all_directly"},
      {"cluster.cluster_0.upstream_cx_max_duration_reached",
       "cluster.upstream_cx_max_duration_reached"},
      {"http.admin.downstream_rq_4xx", "http.downstream_rq_xx"},
      {"http.config_test.downstream_cx_destroy_remote", "http.downstream_cx_destroy_remote"},
      {"http.admin.downstream_cx_protocol_error", "http.downstream_cx_protocol_error"},
      {"listener.admin.http.admin.downstream_rq_completed",
       "listener.admin.http.downstream_rq_completed"},
      {"cluster.cluster_0.membership_change", "cluster.membership_change"},
      {"cluster.cluster_0.upstream_cx_max_requests", "cluster.upstream_cx_max_requests"},
      {"cluster.cluster_0.assignment_stale", "cluster.assignment_stale"},
      {"http.config_test.passthrough_internal_redirect_predicate",
       "http.passthrough_internal_redirect_predicate"},
      {"listener.admin.http.admin.downstream_rq_5xx", "listener.admin.http.downstream_rq_xx"},
      {"cluster_manager.update_out_of_merge_window", "cluster_manager.update_out_of_merge_window"},
      {"server.static_unknown_fields", "server.static_unknown_fields"},
      {"http.admin.downstream_cx_destroy", "http.downstream_cx_destroy"},
      {"listener.admin.downstream_cx_overflow", "listener.admin.downstream_cx_overflow"},
      {"cluster.cluster_0.client_ssl_socket_factory.upstream_context_secrets_not_ready",
       "cluster.client_ssl_socket_factory.upstream_context_secrets_not_ready"},
      {"http.admin.downstream_cx_destroy_active_rq", "http.downstream_cx_destroy_active_rq"},
      {"http.admin.downstream_rq_rx_reset", "http.downstream_rq_rx_reset"},
      {"http.admin.downstream_cx_idle_timeout", "http.downstream_cx_idle_timeout"},
      {"cluster.cluster_0.upstream_rq_cancelled", "cluster.upstream_rq_cancelled"},
      {"http.config_test.downstream_cx_idle_timeout", "http.downstream_cx_idle_timeout"},
      {"http.config_test.downstream_rq_response_before_rq_complete",
       "http.downstream_rq_response_before_rq_complete"},
      {"server.dynamic_unknown_fields", "server.dynamic_unknown_fields"},
      {"http.config_test.downstream_cx_max_requests_reached",
       "http.downstream_cx_max_requests_reached"},
      {"listener.admin.downstream_pre_cx_timeout", "listener.admin.downstream_pre_cx_timeout"},
      {"http.config_test.downstream_cx_delayed_close_timeout",
       "http.downstream_cx_delayed_close_timeout"},
      {"cluster.cluster_0.upstream_cx_close_notify", "cluster.upstream_cx_close_notify"},
      {"cluster.cluster_0.upstream_flow_control_backed_up_total",
       "cluster.upstream_flow_control_backed_up_total"},
      {"listener.admin.http.admin.downstream_rq_3xx", "listener.admin.http.downstream_rq_xx"},
      {"cluster.cluster_0.ssl.handshake", "cluster.ssl.handshake"},
      {"http.config_test.downstream_flow_control_paused_reading_total",
       "http.downstream_flow_control_paused_reading_total"},
      {"cluster.cluster_0.upstream_cx_destroy_remote", "cluster.upstream_cx_destroy_remote"},
      {"listener_manager.lds.update_success", "listener_manager.lds.update_success"},
      {"http.config_test.downstream_rq_2xx", "http.downstream_rq_xx"},
      {"cluster.cluster_0.upstream_rq_retry", "cluster.upstream_rq_retry"},
      {"http.admin.downstream_rq_redirected_with_normalized_path",
       "http.downstream_rq_redirected_with_normalized_path"},
      {"cluster.cluster_0.lb_zone_routing_sampled", "cluster.lb_zone_routing_sampled"},
      {"http.admin.downstream_cx_http1_total", "http.downstream_cx_http1_total"},
      {"cluster.cluster_0.lb_zone_no_capacity_left", "cluster.lb_zone_no_capacity_left"},
      {"http.admin.downstream_cx_max_duration_reached", "http.downstream_cx_max_duration_reached"},
      {"cluster.cluster_0.assignment_timeout_received", "cluster.assignment_timeout_received"},
      {"listener.admin.http.admin.downstream_rq_1xx", "listener.admin.http.downstream_rq_xx"},
      {"cluster.cluster_0.upstream_flow_control_paused_reading_total",
       "cluster.upstream_flow_control_paused_reading_total"},
      {"main_thread.watchdog_miss", "main_thread.watchdog_miss"},
      {"http.config_test.downstream_rq_completed", "http.downstream_rq_completed"},
      {"cluster.cluster_0.upstream_cx_protocol_error", "cluster.upstream_cx_protocol_error"},
      {"listener.admin.downstream_cx_destroy", "listener.admin.downstream_cx_destroy"}};

  tag_extracted_gauge_map = {
      {listenerStatPrefix("worker_0.downstream_cx_active"), "listener.worker_downstream_cx_active"},
      {listenerStatPrefix("downstream_cx_active"), "listener.downstream_cx_active"},
      {listenerStatPrefix("downstream_pre_cx_active"), "listener.downstream_pre_cx_active"},
      {"listener.admin.downstream_cx_active", "listener.admin.downstream_cx_active"},
      {"listener_manager.workers_started", "listener_manager.workers_started"},
      {"http.admin.downstream_cx_upgrades_active", "http.downstream_cx_upgrades_active"},
      {"server.live", "server.live"},
      {"cluster.cluster_0.circuit_breakers.default.rq_open",
       "cluster.circuit_breakers.default.rq_open"},
      {"http.config_test.downstream_cx_http3_active", "http.downstream_cx_http3_active"},
      {"listener_manager.lds.update_time", "listener_manager.lds.update_time"},
      {"cluster_manager.active_clusters", "cluster_manager.active_clusters"},
      {"http.admin.downstream_cx_http2_active", "http.downstream_cx_http2_active"},
      {"runtime.admin_overrides_active", "runtime.admin_overrides_active"},
      {"server.memory_heap_size", "server.memory_heap_size"},
      {"server.compilation_settings.fips_mode", "server.compilation_settings.fips_mode"},
      {"cluster.cluster_0.circuit_breakers.default.cx_pool_open",
       "cluster.circuit_breakers.default.cx_pool_open"},
      {"cluster.cluster_0.membership_degraded", "cluster.membership_degraded"},
      {"cluster.cluster_0.membership_healthy", "cluster.membership_healthy"},
      {"http.config_test.downstream_cx_upgrades_active", "http.downstream_cx_upgrades_active"},
      {"cluster.cluster_0.circuit_breakers.high.rq_pending_open",
       "cluster.circuit_breakers.high.rq_pending_open"},
      {"server.state", "server.state"},
      {"cluster.cluster_0.circuit_breakers.high.cx_pool_open",
       "cluster.circuit_breakers.high.cx_pool_open"},
      {"http.admin.downstream_cx_http3_active", "http.downstream_cx_http3_active"},
      {"cluster.cluster_0.membership_excluded", "cluster.membership_excluded"},
      {"server.concurrency", "server.concurrency"},
      {"runtime.num_layers", "runtime.num_layers"},
      {"listener.admin.downstream_pre_cx_active", "listener.admin.downstream_pre_cx_active"},
      {"http.config_test.downstream_cx_tx_bytes_buffered", "http.downstream_cx_tx_bytes_buffered"},
      {"http.admin.downstream_rq_active", "http.downstream_rq_active"},
      {"listener_manager.total_listeners_draining", "listener_manager.total_listeners_draining"},
      {"listener_manager.lds.version", "listener_manager.lds.version"},
      {"cluster.cluster_0.upstream_cx_rx_bytes_buffered", "cluster.upstream_cx_rx_bytes_buffered"},
      {"http.config_test.downstream_cx_ssl_active", "http.downstream_cx_ssl_active"},
      {"cluster.cluster_0.membership_total", "cluster.membership_total"},
      {"cluster.cluster_0.circuit_breakers.default.rq_retry_open",
       "cluster.circuit_breakers.default.rq_retry_open"},
      {"cluster_manager.warming_clusters", "cluster_manager.warming_clusters"},
      {"cluster.cluster_0.circuit_breakers.default.cx_open",
       "cluster.circuit_breakers.default.cx_open"},
      {"cluster.cluster_0.upstream_rq_active", "cluster.upstream_rq_active"},
      {"server.seconds_until_first_ocsp_response_expiring",
       "server.seconds_until_first_ocsp_response_expiring"},
      {"server.memory_allocated", "server.memory_allocated"},
      {"cluster.cluster_0.max_host_weight", "cluster.max_host_weight"},
      {"http.config_test.downstream_cx_active", "http.downstream_cx_active"},
      {"filesystem.write_total_buffered", "filesystem.write_total_buffered"},
      {"http.config_test.downstream_rq_active", "http.downstream_rq_active"},
      {"cluster.cluster_0.circuit_breakers.high.rq_open", "cluster.circuit_breakers.high.rq_open"},
      {"listener.admin.main_thread.downstream_cx_active",
       "listener.admin.main_thread.downstream_cx_active"},
      {"server.stats_recent_lookups", "server.stats_recent_lookups"},
      {"cluster.cluster_0.circuit_breakers.default.rq_pending_open",
       "cluster.circuit_breakers.default.rq_pending_open"},
      {"server.uptime", "server.uptime"},
      {"server.days_until_first_cert_expiring", "server.days_until_first_cert_expiring"},
      {"cluster.cluster_0.upstream_rq_pending_active", "cluster.upstream_rq_pending_active"},
      {"http.admin.downstream_cx_http1_active", "http.downstream_cx_http1_active"},
      {"listener_manager.total_listeners_active", "listener_manager.total_listeners_active"},
      {"server.memory_physical_size", "server.memory_physical_size"},
      {"cluster.cluster_0.lb_subsets_active", "cluster.lb_subsets_active"},
      {"listener_manager.total_filter_chains_draining",
       "listener_manager.total_filter_chains_draining"},
      {"cluster.cluster_0.version", "cluster.version"},
      {"cluster.cluster_0.circuit_breakers.high.cx_open", "cluster.circuit_breakers.high.cx_open"},
      {"server.version", "server.version"},
      {"http.admin.downstream_cx_rx_bytes_buffered", "http.downstream_cx_rx_bytes_buffered"},
      {"listener_manager.total_listeners_warming", "listener_manager.total_listeners_warming"},
      {"server.hot_restart_epoch", "server.hot_restart_epoch"},
      {"http.config_test.downstream_cx_http1_active", "http.downstream_cx_http1_active"},
      {"http.admin.downstream_cx_tx_bytes_buffered", "http.downstream_cx_tx_bytes_buffered"},
      {"http.admin.downstream_cx_active", "http.downstream_cx_active"},
      {"http.admin.downstream_cx_ssl_active", "http.downstream_cx_ssl_active"},
      {"cluster.cluster_0.upstream_cx_tx_bytes_buffered", "cluster.upstream_cx_tx_bytes_buffered"},
      {"http.config_test.downstream_cx_http2_active", "http.downstream_cx_http2_active"},
      {"http.config_test.downstream_cx_rx_bytes_buffered", "http.downstream_cx_rx_bytes_buffered"},
      {"cluster.cluster_0.circuit_breakers.high.rq_retry_open",
       "cluster.circuit_breakers.high.rq_retry_open"},
      {"runtime.deprecated_feature_seen_since_process_start",
       "runtime.deprecated_feature_seen_since_process_start"},
      {"runtime.num_keys", "runtime.num_keys"},
      {"server.parent_connections", "server.parent_connections"},
      {"server.total_connections", "server.total_connections"},
      {"cluster.cluster_0.upstream_cx_active", "cluster.upstream_cx_active"}};

  auto test_name_against_mapping = [](MetricMap& extracted_name_map, const Stats::Metric& metric) {
    auto it = extracted_name_map.find(metric.name());
    // Ignore any metrics that are not found in the map for ease of addition
    if (it != extracted_name_map.end()) {
      // Check that the tag extracted name matches the "golden" state.
      EXPECT_EQ(it->second, metric.tagExtractedName());
      extracted_name_map.erase(it);
    }
  };

  for (const Stats::CounterSharedPtr& counter : test_server_->counters()) {
    test_name_against_mapping(tag_extracted_counter_map, *counter);
  }
  EXPECT_EQ(tag_extracted_counter_map, MetricMap{});

  for (const Stats::GaugeSharedPtr& gauge : test_server_->gauges()) {
    test_name_against_mapping(tag_extracted_gauge_map, *gauge);
  }
  EXPECT_EQ(tag_extracted_gauge_map, MetricMap{});
}
} // namespace Xfcc
} // namespace Envoy
