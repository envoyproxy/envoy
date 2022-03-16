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
#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/extensions/transport_sockets/tls/context_manager_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

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

Network::TransportSocketFactoryPtr XfccIntegrationTest::createClientSslContext(bool mtls) {
  const std::string yaml_tls = R"EOF(
common_tls_context:
  validation_context:
    trusted_ca:
      filename: {{ test_rundir }}/test/config/integration/certs/cacert.pem
    match_typed_subject_alt_names:
    - san_type: URI
      matcher:
        exact: "spiffe://lyft.com/backend-team"
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
  auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ClientContextConfigImpl>(
      config, factory_context_);
  static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  return Network::TransportSocketFactoryPtr{
      new Extensions::TransportSockets::Tls::ClientSslSocketFactory(
          std::move(cfg), *context_manager_, *client_stats_store)};
}

Network::TransportSocketFactoryPtr XfccIntegrationTest::createUpstreamSslContext() {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  auto* common_tls_context = tls_context.mutable_common_tls_context();
  auto* tls_cert = common_tls_context->add_tls_certificates();
  tls_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
  tls_cert->mutable_private_key()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));

  auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
      tls_context, factory_context_);
  static Stats::Scope* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
  return std::make_unique<Extensions::TransportSockets::Tls::ServerSslSocketFactory>(
      std::move(cfg), *context_manager_, *upstream_stats_store, std::vector<std::string>{});
}

Network::ClientConnectionPtr XfccIntegrationTest::makeTcpClientConnection() {
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://" + Network::Test::getLoopbackAddressUrlString(version_) +
                                   ":" + std::to_string(lookupPort("http")));
  return dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                             Network::Test::createRawBufferSocket(), nullptr);
}

Network::ClientConnectionPtr XfccIntegrationTest::makeMtlsClientConnection() {
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://" + Network::Test::getLoopbackAddressUrlString(version_) +
                                   ":" + std::to_string(lookupPort("http")));
  return dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                             client_mtls_ssl_ctx_->createTransportSocket(nullptr),
                                             nullptr);
}

void XfccIntegrationTest::createUpstreams() {
  addFakeUpstream(createUpstreamSslContext(), Http::CodecType::HTTP1);
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

  context_manager_ =
      std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());
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
  initialize();

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
      {listenerStatPrefix("downstream_cx_total"), "listener.downstream_cx_total"},
      {listenerStatPrefix("downstream_cx_destroy"), "listener.downstream_cx_destroy"},
      {listenerStatPrefix("ssl.connection_error"), "listener.ssl.connection_error"},
      {listenerStatPrefix("ssl.handshake"), "listener.ssl.handshake"},
      {listenerStatPrefix("ssl.session_reused"), "listener.ssl.session_reused"},
      {listenerStatPrefix("ssl.fail_verify_san"), "listener.ssl.fail_verify_san"},
      {listenerStatPrefix("ssl.no_certificate"), "listener.ssl.no_certificate"},
      {listenerStatPrefix("ssl.fail_verify_no_cert"), "listener.ssl.fail_verify_no_cert"},
      {listenerStatPrefix("ssl.fail_verify_error"), "listener.ssl.fail_verify_error"},
      {listenerStatPrefix("ssl.fail_verify_cert_hash"), "listener.ssl.fail_verify_cert_hash"},
      {"listener.admin.downstream_cx_destroy", "listener.admin.downstream_cx_destroy"},
      {"listener.admin.downstream_cx_total", "listener.admin.downstream_cx_total"},
      {"cluster_manager.cluster_added", "cluster_manager.cluster_added"},
      {"http.admin.downstream_rq_http2_total", "http.downstream_rq_http2_total"},
      {"cluster_manager.cluster_removed", "cluster_manager.cluster_removed"},
      {"http.admin.downstream_cx_destroy_remote", "http.downstream_cx_destroy_remote"},
      {"http.admin.downstream_rq_http1_total", "http.downstream_rq_http1_total"},
      {"http.admin.downstream_rq_total", "http.downstream_rq_total"},
      {"http.admin.downstream_cx_rx_bytes_total", "http.downstream_cx_rx_bytes_total"},
      {"http.admin.downstream_cx_destroy_remote_active_rq",
       "http.downstream_cx_destroy_remote_active_rq"},
      {"http.admin.downstream_cx_destroy_local_active_rq",
       "http.downstream_cx_destroy_local_active_rq"},
      {"filesystem.write_buffered", "filesystem.write_buffered"},
      {"http.admin.downstream_cx_destroy_active_rq", "http.downstream_cx_destroy_active_rq"},
      {"http.admin.downstream_rq_tx_reset", "http.downstream_rq_tx_reset"},
      {"http.admin.downstream_flow_control_resumed_reading_total",
       "http.downstream_flow_control_resumed_reading_total"},
      {"http.admin.downstream_cx_total", "http.downstream_cx_total"},
      {"http.admin.downstream_rq_3xx", "http.downstream_rq_xx"},
      {"http.admin.downstream_cx_idle_timeout", "http.downstream_cx_idle_timeout"},
      {"http.admin.downstream_rq_rx_reset", "http.downstream_rq_rx_reset"},
      {"http.admin.downstream_cx_ssl_total", "http.downstream_cx_ssl_total"},
      {"http.admin.downstream_rq_2xx", "http.downstream_rq_xx"},
      {"cluster_manager.cluster_modified", "cluster_manager.cluster_modified"},
      {"http.admin.downstream_cx_drain_close", "http.downstream_cx_drain_close"},
      {"http.admin.downstream_cx_destroy", "http.downstream_cx_destroy"},
      {"http.admin.downstream_cx_http1_total", "http.downstream_cx_http1_total"},
      {"http.admin.downstream_cx_protocol_error", "http.downstream_cx_protocol_error"},
      {"http.admin.downstream_cx_destroy_local", "http.downstream_cx_destroy_local"},
      {"listener_manager.listener_added", "listener_manager.listener_added"},
      {"filesystem.write_completed", "filesystem.write_completed"},
      {"http.admin.downstream_rq_response_before_rq_complete",
       "http.downstream_rq_response_before_rq_complete"},
      {"http.admin.downstream_cx_tx_bytes_total", "http.downstream_cx_tx_bytes_total"},
      {"http.admin.downstream_rq_4xx", "http.downstream_rq_xx"},
      {"http.admin.downstream_rq_non_relative_path", "http.downstream_rq_non_relative_path"},
      {"http.admin.downstream_rq_ws_on_non_ws_route", "http.downstream_rq_ws_on_non_ws_route"},
      {"http.admin.downstream_rq_too_large", "http.downstream_rq_too_large"},
      {"http.admin.downstream_rq_5xx", "http.downstream_rq_xx"},
      {"http.admin.downstream_flow_control_paused_reading_total",
       "http.downstream_flow_control_paused_reading_total"},
      {"listener_manager.listener_removed", "listener_manager.listener_removed"},
      {"listener_manager.listener_create_failure", "listener_manager.listener_create_failure"},
      {"filesystem.flushed_by_timer", "filesystem.flushed_by_timer"},
      {"http.admin.downstream_cx_http2_total", "http.downstream_cx_http2_total"},
      {"filesystem.reopen_failed", "filesystem.reopen_failed"},
      {"listener_manager.listener_modified", "listener_manager.listener_modified"},
      {"http.admin.rs_too_large", "http.rs_too_large"},
      {"listener_manager.listener_create_success", "listener_manager.listener_create_success"}};
  tag_extracted_gauge_map = {
      {listenerStatPrefix("downstream_cx_active"), "listener.downstream_cx_active"},
      {"listener.admin.downstream_cx_active", "listener.admin.downstream_cx_active"},
      {"listener_manager.total_listeners_warming", "listener_manager.total_listeners_warming"},
      {"listener_manager.total_listeners_active", "listener_manager.total_listeners_active"},
      {"http.admin.downstream_rq_active", "http.downstream_rq_active"},
      {"http.admin.downstream_cx_tx_bytes_buffered", "http.downstream_cx_tx_bytes_buffered"},
      {"http.admin.downstream_cx_rx_bytes_buffered", "http.downstream_cx_rx_bytes_buffered"},
      {"http.admin.downstream_cx_http1_active", "http.downstream_cx_http1_active"},
      {"server.uptime", "server.uptime"},
      {"server.memory_allocated", "server.memory_allocated"},
      {"http.admin.downstream_cx_http2_active", "http.downstream_cx_http2_active"},
      {"server.memory_heap_size", "server.memory_heap_size"},
      {"server.memory_physical_size", "server.memory_physical_size"},
      {"listener_manager.total_listeners_draining", "listener_manager.total_listeners_draining"},
      {"filesystem.write_total_buffered", "filesystem.write_total_buffered"},
      {"http.admin.downstream_cx_ssl_active", "http.downstream_cx_ssl_active"},
      {"http.admin.downstream_cx_active", "http.downstream_cx_active"},
      {"server.live", "server.live"},
      {"server.parent_connections", "server.parent_connections"},
      {"server.total_connections", "server.total_connections"},
      {"server.days_until_first_cert_expiring", "server.days_until_first_cert_expiring"},
      {"server.seconds_until_first_ocsp_response_expiring",
       "server.seconds_until_first_ocsp_response_expiring"},
      {"server.version", "server.version"}};

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
