#include "envoy/admin/v3/memory.pb.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"

#include "test/server/admin/admin_instance.h"
#include "test/test_common/logging.h"

using testing::Ge;
using testing::HasSubstr;
using testing::Property;
using testing::Return;

namespace Envoy {
namespace Server {

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminInstanceTest, ContextThatReturnsNullCertDetails) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  // Setup a context that returns null cert details.
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext config;
  Extensions::TransportSockets::Tls::ClientContextConfigImpl cfg(config, factory_context);
  Stats::IsolatedStoreImpl store;
  Envoy::Ssl::ClientContextSharedPtr client_ctx(
      server_.sslContextManager().createSslClientContext(store, cfg));

  const std::string expected_empty_json = R"EOF({
 "certificates": [
  {
   "ca_cert": [],
   "cert_chain": []
  }
 ]
}
)EOF";

  // Validate that cert details are null and /certs handles it correctly.
  EXPECT_EQ(nullptr, client_ctx->getCaCertInformation());
  EXPECT_TRUE(client_ctx->getCertChainInformation().empty());
  EXPECT_EQ(Http::Code::OK, getCallback("/certs", header_map, response));
  EXPECT_EQ(expected_empty_json, response.toString());
}

TEST_P(AdminInstanceTest, Memory) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, getCallback("/memory", header_map, response));
  const std::string output_json = response.toString();
  envoy::admin::v3::Memory output_proto;
  TestUtility::loadFromJson(output_json, output_proto);
  EXPECT_THAT(output_proto, AllOf(Property(&envoy::admin::v3::Memory::allocated, Ge(0)),
                                  Property(&envoy::admin::v3::Memory::heap_size, Ge(0)),
                                  Property(&envoy::admin::v3::Memory::pageheap_unmapped, Ge(0)),
                                  Property(&envoy::admin::v3::Memory::pageheap_free, Ge(0)),
                                  Property(&envoy::admin::v3::Memory::total_thread_cache, Ge(0))));
}

TEST_P(AdminInstanceTest, GetReadyRequest) {
  NiceMock<Init::MockManager> initManager;
  ON_CALL(server_, initManager()).WillByDefault(ReturnRef(initManager));

  {
    Http::TestResponseHeaderMapImpl response_headers;
    std::string body;

    ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Initialized));
    EXPECT_EQ(Http::Code::OK, admin_.request("/ready", "GET", response_headers, body));
    EXPECT_EQ(body, "LIVE\n");
    EXPECT_THAT(std::string(response_headers.getContentTypeValue()), HasSubstr("text/plain"));
  }

  {
    Http::TestResponseHeaderMapImpl response_headers;
    std::string body;

    ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Uninitialized));
    EXPECT_EQ(Http::Code::ServiceUnavailable,
              admin_.request("/ready", "GET", response_headers, body));
    EXPECT_EQ(body, "PRE_INITIALIZING\n");
    EXPECT_THAT(std::string(response_headers.getContentTypeValue()), HasSubstr("text/plain"));
  }

  Http::TestResponseHeaderMapImpl response_headers;
  std::string body;

  ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Initializing));
  EXPECT_EQ(Http::Code::ServiceUnavailable,
            admin_.request("/ready", "GET", response_headers, body));
  EXPECT_EQ(body, "INITIALIZING\n");
  EXPECT_THAT(std::string(response_headers.getContentTypeValue()), HasSubstr("text/plain"));
}

TEST_P(AdminInstanceTest, GetRequest) {
  EXPECT_CALL(server_.options_, toCommandLineOptions()).WillRepeatedly(Invoke([] {
    Server::CommandLineOptionsPtr command_line_options =
        std::make_unique<envoy::admin::v3::CommandLineOptions>();
    command_line_options->set_restart_epoch(2);
    command_line_options->set_service_cluster("cluster");
    return command_line_options;
  }));
  NiceMock<Init::MockManager> initManager;
  ON_CALL(server_, initManager()).WillByDefault(ReturnRef(initManager));
  ON_CALL(server_.hot_restart_, version()).WillByDefault(Return("foo_version"));

  {
    Http::TestResponseHeaderMapImpl response_headers;
    std::string body;

    ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Initialized));
    EXPECT_EQ(Http::Code::OK, admin_.request("/server_info", "GET", response_headers, body));
    envoy::admin::v3::ServerInfo server_info_proto;
    EXPECT_THAT(std::string(response_headers.getContentTypeValue()), HasSubstr("application/json"));

    // We only test that it parses as the proto and that some fields are correct, since
    // values such as timestamps + Envoy version are tricky to test for.
    TestUtility::loadFromJson(body, server_info_proto);
    EXPECT_EQ(server_info_proto.state(), envoy::admin::v3::ServerInfo::LIVE);
    EXPECT_EQ(server_info_proto.hot_restart_version(), "foo_version");
    EXPECT_EQ(server_info_proto.command_line_options().restart_epoch(), 2);
    EXPECT_EQ(server_info_proto.command_line_options().service_cluster(), "cluster");
  }

  {
    Http::TestResponseHeaderMapImpl response_headers;
    std::string body;

    ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Uninitialized));
    EXPECT_EQ(Http::Code::OK, admin_.request("/server_info", "GET", response_headers, body));
    envoy::admin::v3::ServerInfo server_info_proto;
    EXPECT_THAT(std::string(response_headers.getContentTypeValue()), HasSubstr("application/json"));

    // We only test that it parses as the proto and that some fields are correct, since
    // values such as timestamps + Envoy version are tricky to test for.
    TestUtility::loadFromJson(body, server_info_proto);
    EXPECT_EQ(server_info_proto.state(), envoy::admin::v3::ServerInfo::PRE_INITIALIZING);
    EXPECT_EQ(server_info_proto.command_line_options().restart_epoch(), 2);
    EXPECT_EQ(server_info_proto.command_line_options().service_cluster(), "cluster");
  }

  Http::TestResponseHeaderMapImpl response_headers;
  std::string body;

  ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Initializing));
  EXPECT_EQ(Http::Code::OK, admin_.request("/server_info", "GET", response_headers, body));
  envoy::admin::v3::ServerInfo server_info_proto;
  EXPECT_THAT(std::string(response_headers.getContentTypeValue()), HasSubstr("application/json"));

  // We only test that it parses as the proto and that some fields are correct, since
  // values such as timestamps + Envoy version are tricky to test for.
  TestUtility::loadFromJson(body, server_info_proto);
  EXPECT_EQ(server_info_proto.state(), envoy::admin::v3::ServerInfo::INITIALIZING);
  EXPECT_EQ(server_info_proto.command_line_options().restart_epoch(), 2);
  EXPECT_EQ(server_info_proto.command_line_options().service_cluster(), "cluster");
}

TEST_P(AdminInstanceTest, PostRequest) {
  Http::TestResponseHeaderMapImpl response_headers;
  std::string body;
  EXPECT_NO_LOGS(EXPECT_EQ(Http::Code::OK,
                           admin_.request("/healthcheck/fail", "POST", response_headers, body)));
  EXPECT_EQ(body, "OK\n");
  EXPECT_THAT(std::string(response_headers.getContentTypeValue()), HasSubstr("text/plain"));
}

} // namespace Server
} // namespace Envoy
