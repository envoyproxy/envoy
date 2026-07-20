#include <sstream>
#include <vector>

#include "envoy/service/redis_auth/v3/redis_external_auth.pb.h"

#include "source/common/common/fmt.h"
#include "source/common/common/random_generator.h"
#include "source/extensions/filters/network/common/redis/fault_impl.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter_impl.h"
#include "source/extensions/network/dns_resolver/getaddrinfo/getaddrinfo.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/integration.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace RedisCmdSplitter = Envoy::Extensions::NetworkFilters::RedisProxy::CommandSplitter;

namespace Envoy {
namespace {

// This is a basic redis_proxy configuration with 2 endpoints/hosts
// in the cluster. The load balancing policy must be set
// to random for proper test operation.

const std::string CONFIG = fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    - name: cluster_0
      type: STATIC
      lb_policy: RANDOM
      load_assignment:
        cluster_name: cluster_0
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
  listeners:
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: redis
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProxy
          stat_prefix: redis_stats
          prefix_routes:
            catch_all_route:
              cluster: cluster_0
          settings:
            op_timeout: 5s
)EOF",
                                       Platform::null_device_path);

// This is a configuration with command stats enabled.
const std::string CONFIG_WITH_COMMAND_STATS = CONFIG + R"EOF(
            enable_command_stats: true
)EOF";

// This is a configuration with moved/ask redirection support enabled.
const std::string CONFIG_WITH_REDIRECTION = CONFIG + R"EOF(
            enable_redirection: true
)EOF";

// This is a configuration with moved/ask redirection support and DNS lookups enabled.
constexpr absl::string_view CONFIG_WITH_REDIRECTION_DNS = R"EOF({}
            dns_cache_config:
              name: foo
              dns_lookup_family: {}
              max_hosts: 100
              typed_dns_resolver_config:
                name: envoy.network.dns_resolver.getaddrinfo
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig
)EOF";

// This is a configuration with batching enabled.
const std::string CONFIG_WITH_BATCHING = CONFIG + R"EOF(
            max_buffer_size_before_flush: 1024
            buffer_flush_timeout: 0.003s
)EOF";

// RESP3 listener: protocol_version: RESP3 forces both downstream and routed-upstream onto
// RESP3. Pre-HELLO data commands are gated -NOPROTO; HELLO 3 must precede any data command.
const std::string CONFIG_RESP3_LISTENER = CONFIG + R"EOF(
          protocol_version: RESP3
)EOF";

// RESP3 listener with a downstream password configured, for HELLO 3 AUTH inline-auth coverage.
// Uses the non-deprecated repeated ``downstream_auth_passwords`` so the config loads under the
// compile_time_options build (``deprecated_features=disabled`` makes the singular
// ``downstream_auth_password`` a fatal config rejection).
const std::string CONFIG_RESP3_LISTENER_WITH_AUTH = CONFIG_RESP3_LISTENER + R"EOF(
          downstream_auth_passwords:
          - inline_string: somepassword
)EOF";

// RESP3 listener with a non-Primary read policy: every routed upstream must negotiate HELLO 3,
// then READONLY, before any user command. read_policy is a ConnPoolSettings field (12-space
// indent, inside settings); protocol_version is a RedisProxy field (10-space indent).
const std::string CONFIG_RESP3_PREFER_REPLICA = CONFIG + R"EOF(
            read_policy: PREFER_REPLICA
          protocol_version: RESP3
)EOF";

// This is a configuration with custom commands.
const std::string CONFIG_WITH_CUSTOM_COMMANDS = CONFIG + R"EOF(
          custom_commands:
          - json.set
)EOF";

const std::string CONFIG_WITH_ROUTES_BASE = fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    - name: cluster_0
      type: STATIC
      lb_policy: RANDOM
      load_assignment:
        cluster_name: cluster_0
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
    - name: cluster_1
      type: STATIC
      lb_policy: RANDOM
      load_assignment:
        cluster_name: cluster_1
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
    - name: cluster_2
      type: STATIC
      lb_policy: RANDOM
      load_assignment:
        cluster_name: cluster_2
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
  listeners:
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: redis
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProxy
          stat_prefix: redis_stats
          settings:
            op_timeout: 5s
)EOF",
                                                        Platform::null_device_path);

const std::string CONFIG_WITH_ROUTES = CONFIG_WITH_ROUTES_BASE + R"EOF(
          prefix_routes:
            catch_all_route:
              cluster: cluster_0
            routes:
            - prefix: "foo:"
              cluster: cluster_1
            - prefix: "baz:"
              cluster: cluster_2
)EOF";

const std::string CONFIG_WITH_MIRROR = CONFIG_WITH_ROUTES_BASE + R"EOF(
          prefix_routes:
            catch_all_route:
              cluster: cluster_0
              request_mirror_policy:
              - cluster: cluster_1
              - cluster: cluster_2
            routes:
            - prefix: "write_only:"
              cluster: cluster_0
              request_mirror_policy:
              - cluster: cluster_1
                exclude_read_commands: true
            - prefix: "percentage:"
              cluster: cluster_0
              request_mirror_policy:
              - cluster: cluster_1
                runtime_fraction:
                  default_value:
                    numerator: 50
                    denominator: HUNDRED
                  runtime_key: "bogus_key"
)EOF";

// RESP3 + mirror: the filter-level protocol_version applies to the primary and every mirror conn
// pool, so each negotiates HELLO 3 independently. Appended at the RedisProxy field level
// (10-space indent), a sibling of the mirror prefix_routes block above.
const std::string CONFIG_RESP3_WITH_MIRROR = CONFIG_WITH_MIRROR + R"EOF(
          protocol_version: RESP3
)EOF";

const std::string CONFIG_WITH_DOWNSTREAM_AUTH_PASSWORD_SET = CONFIG + R"EOF(
          downstream_auth_password: { inline_string: somepassword }
)EOF";

const std::string CONFIG_WITH_MULTIPLE_DOWNSTREAM_AUTH_PASSWORDS_SET = CONFIG + R"EOF(
          downstream_auth_passwords:
          - inline_string: somepassword
          - inline_string: someotherpassword
)EOF";

const std::string CONFIG_WITH_ROUTES_AND_AUTH_PASSWORDS = fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    - name: cluster_0
      type: STATIC
      typed_extension_protocol_options:
        envoy.filters.network.redis_proxy:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProtocolOptions
          auth_password: {{ inline_string: cluster_0_password }}
      lb_policy: RANDOM
      load_assignment:
        cluster_name: cluster_0
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
    - name: cluster_1
      type: STATIC
      lb_policy: RANDOM
      typed_extension_protocol_options:
        envoy.filters.network.redis_proxy:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProtocolOptions
          auth_password: {{ inline_string: cluster_1_password }}
      load_assignment:
        cluster_name: cluster_1
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
    - name: cluster_2
      type: STATIC
      typed_extension_protocol_options:
        envoy.filters.network.redis_proxy:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProtocolOptions
          auth_password: {{ inline_string: cluster_2_password }}
      lb_policy: RANDOM
      load_assignment:
        cluster_name: cluster_2
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
  listeners:
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: redis
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProxy
          stat_prefix: redis_stats
          settings:
            op_timeout: 5s
          prefix_routes:
            catch_all_route:
              cluster: cluster_0
            routes:
            - prefix: "foo:"
              cluster: cluster_1
            - prefix: "baz:"
              cluster: cluster_2
)EOF",
                                                                      Platform::null_device_path);

const std::string CONFIG_WITH_ROUTES_AND_AUTH_PASSWORDS_AWS_IAM =
    fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    - name: cluster_0
      type: STATIC
      typed_extension_protocol_options:
        envoy.filters.network.redis_proxy:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProtocolOptions
          auth_username: {{ inline_string: cluster_0_username }}
          aws_iam:
            region: us-east-1
            service_name: elasticache
            cache_name: testcache
            expiration_time: 900s
      lb_policy: RANDOM
      load_assignment:
        cluster_name: cluster_0
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
    - name: cluster_1
      type: STATIC
      lb_policy: RANDOM
      typed_extension_protocol_options:
        envoy.filters.network.redis_proxy:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProtocolOptions
          auth_username: {{ inline_string: cluster_1_username }}
          aws_iam:
            region: us-east-1
            service_name: elasticache
            cache_name: testcache
            expiration_time: 900s
      load_assignment:
        cluster_name: cluster_1
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
    - name: cluster_2
      type: STATIC
      typed_extension_protocol_options:
        envoy.filters.network.redis_proxy:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProtocolOptions
          auth_username: {{ inline_string: cluster_2_username }}
          aws_iam:
            region: us-east-1
            service_name: elasticache
            cache_name: testcache
            expiration_time: 900s
      lb_policy: RANDOM
      load_assignment:
        cluster_name: cluster_2
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
  listeners:
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: redis
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProxy
          stat_prefix: redis_stats
          settings:
            op_timeout: 5s
          prefix_routes:
            catch_all_route:
              cluster: cluster_0
            routes:
            - prefix: "foo:"
              cluster: cluster_1
            - prefix: "baz:"
              cluster: cluster_2
)EOF",
                Platform::null_device_path);

const std::string CONFIG_WITH_ROUTES_AND_AUTH_PASSWORDS_AWS_IAM_NO_AUTH_USERNAME =
    fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    - name: cluster_0
      type: STATIC
      typed_extension_protocol_options:
        envoy.filters.network.redis_proxy:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProtocolOptions
          aws_iam:
            region: us-east-1
            service_name: elasticache
            cache_name: testcache
            expiration_time: 900s
      lb_policy: RANDOM
      load_assignment:
        cluster_name: cluster_0
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
    - name: cluster_1
      type: STATIC
      lb_policy: RANDOM
      typed_extension_protocol_options:
        envoy.filters.network.redis_proxy:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProtocolOptions
          auth_username: {{ inline_string: cluster_1_username }}
          aws_iam:
            region: us-east-1
            service_name: elasticache
            cache_name: testcache
            expiration_time: 900s
      load_assignment:
        cluster_name: cluster_1
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
    - name: cluster_2
      type: STATIC
      typed_extension_protocol_options:
        envoy.filters.network.redis_proxy:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProtocolOptions
          auth_username: {{ inline_string: cluster_2_username }}
          aws_iam:
            region: us-east-1
            service_name: elasticache
            cache_name: testcache
            expiration_time: 900s
      lb_policy: RANDOM
      load_assignment:
        cluster_name: cluster_2
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
  listeners:
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: redis
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProxy
          stat_prefix: redis_stats
          settings:
            op_timeout: 5s
          prefix_routes:
            catch_all_route:
              cluster: cluster_0
            routes:
            - prefix: "foo:"
              cluster: cluster_1
            - prefix: "baz:"
              cluster: cluster_2
)EOF",
                Platform::null_device_path);

// The `typed_extension_protocol_options` is modified significantly in the test that uses this
// config.
const std::string CONFIG_WITH_SEPARATE_AUTH_PASSWORDS = fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    - name: cluster_0
      type: STATIC
      typed_extension_protocol_options:
        envoy.filters.network.redis_proxy:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProtocolOptions
          auth_username: {{ inline_string: default_endpoint_username }}
          auth_password: {{ inline_string: default_endpoint_password }}
      lb_policy: RING_HASH
      load_assignment:
        cluster_name: cluster_0
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
  listeners:
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: redis
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProxy
          stat_prefix: redis_stats
          settings:
            op_timeout: 5s
          prefix_routes:
            catch_all_route:
              cluster: cluster_0
)EOF",
                                                                    Platform::null_device_path);

// This is a configuration with fault injection enabled.
const std::string CONFIG_WITH_FAULT_INJECTION = CONFIG + R"EOF(
          faults:
          - fault_type: ERROR
            fault_enabled:
              default_value:
                numerator: 100
                denominator: HUNDRED
            commands:
            - GET
          - fault_type: DELAY
            fault_enabled:
              default_value:
                numerator: 20
                denominator: HUNDRED
              runtime_key: "bogus_key"
            delay: 2s
            commands:
            - SET
)EOF";

const std::string CONFIG_WITH_EXTERNAL_AUTH = CONFIG + R"EOF(
          external_auth_provider:
            enable_auth_expiration: true
            grpc_service:
              timeout: 2s
              envoy_grpc:
                cluster_name: fake_auth
)EOF";

// This function encodes commands as an array of bulkstrings as transmitted by Redis clients to
// Redis servers, according to the Redis protocol.
std::string makeBulkStringArray(std::vector<std::string>&& command_strings) {
  std::stringstream result;

  result << "*" << command_strings.size() << "\r\n";
  for (auto& command_string : command_strings) {
    result << "$" << command_string.size() << "\r\n";
    result << command_string << "\r\n";
  }

  return result.str();
}

// Canonical RESP3 HELLO reply emitted locally by the proxy. Pin exact bytes so
// integration tests catch accidental buildHelloReply field/order drift.
std::string resp3HelloMapReply() {
  return "%7\r\n"
         "$6\r\nserver\r\n$17\r\nenvoy-redis-proxy\r\n"
         "$7\r\nversion\r\n$5\r\n6.0.0\r\n"
         "$5\r\nproto\r\n:3\r\n"
         "$2\r\nid\r\n:0\r\n"
         "$4\r\nmode\r\n$10\r\nstandalone\r\n"
         "$4\r\nrole\r\n$6\r\nmaster\r\n"
         "$7\r\nmodules\r\n*0\r\n";
}

// Downstream RESP3 negotiation: HELLO 3, then consume the proxy's local HELLO map reply. The
// size overload of waitForData returns an AssertionResult, so ASSERT_TRUE makes a short/missing
// reply a fatal failure here (the string overload is void and cannot be asserted); ASSERT_EQ then
// pins the exact bytes so a wrong HELLO reply aborts rather than flowing into the rest of the
// test. Callers must wrap this in ASSERT_NO_FATAL_FAILURE so the fatal failure also stops the
// calling test.
void negotiateDownstreamResp3(IntegrationTcpClientPtr& redis_client) {
  ASSERT_TRUE(redis_client->write(makeBulkStringArray({"hello", "3"})));
  const std::string hello_reply = resp3HelloMapReply();
  ASSERT_TRUE(redis_client->waitForData(hello_reply.size()));
  ASSERT_EQ(hello_reply, redis_client->data());
  redis_client->clearData();
}

// Upstream RESP3 negotiation on a freshly-accepted raw upstream connection: assert the proxy sent
// exactly HELLO 3, then reply with a minimal proto:3 map. Appends the HELLO bytes to the caller's
// cumulative accumulator so the per-command ordering assertions stay visible in the test body.
// The internal ASSERT_* only return from this helper, so callers must wrap the call in
// ASSERT_NO_FATAL_FAILURE to propagate a fatal failure to the test.
void expectUpstreamHello3AndReply(FakeRawConnectionPtr& conn, std::string& expected_upstream) {
  const std::string hello = makeBulkStringArray({"HELLO", "3"});
  std::string seen;
  ASSERT_TRUE(conn->waitForData(hello.size(), &seen));
  ASSERT_EQ(hello, seen);
  ASSERT_TRUE(conn->write("%1\r\n$5\r\nproto\r\n:3\r\n"));
  expected_upstream += hello;
}

class RedisProxyIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public BaseIntegrationTest {
public:
  RedisProxyIntegrationTest(const std::string& config = CONFIG, int num_upstreams = 2)
      : BaseIntegrationTest(GetParam(), config), num_upstreams_(num_upstreams),
        version_(GetParam()) {}

  // This method encodes a fake upstream's IP address and TCP port in the
  // same format as one would expect from a Redis server in
  // an ask/moved redirection error.

  std::string redisAddressAndPortNoThrow(FakeUpstreamPtr& upstream) {
    std::stringstream result;
    if (version_ == Network::Address::IpVersion::v4) {
      result << "127.0.0.1"
             << ":";
    } else {
      result << "::1"
             << ":";
    }
    result << upstream->localAddress()->ip()->port();
    return result.str();
  }

  std::string redisHostnameAndPort(FakeUpstreamPtr& upstream) {
    std::stringstream result;
    result << "localhost"
           << ":" << upstream->localAddress()->ip()->port();
    return result.str();
  }

  void initialize() override;

  /**
   * Simple bi-directional test between a fake Redis client and Redis server.
   * @param request supplies Redis client data to transmit to the Redis server.
   * @param response supplies Redis server data to transmit to the client.
   */
  void simpleRequestAndResponse(const std::string& request, const std::string& response) {
    return simpleRoundtripToUpstream(fake_upstreams_[0], request, response);
  }

  /**
   * Simple bi-direction test between a fake redis client and a specific redis server.
   * @param upstream a handle to the server that will respond to the request.
   * @param request supplies Redis client data to transmit to the Redis server.
   * @param response supplies Redis server data to transmit to the client.
   */
  void simpleRoundtripToUpstream(FakeUpstreamPtr& upstream, const std::string& request,
                                 const std::string& response);

  /**
   * Simple bi-directional test between a fake Redis client and proxy server.
   * @param request supplies Redis client data to transmit to the proxy.
   * @param proxy_response supplies proxy data in response to the client's request.
   */
  void simpleProxyResponse(const std::string& request, const std::string& proxy_response);

  /**
   * A single step of a larger test involving a fake Redis client and the proxy server.
   * @param request supplies Redis client data to transmit to the proxy.
   * @param redis_client a handle to the fake redis client that sends the request.
   */
  void proxyRequestStep(const std::string& request, IntegrationTcpClientPtr& redis_client);

  /**
   * A single step of a larger test involving a fake Redis client and the proxy server.
   * @param proxy_response supplies proxy data in response to the client's request.
   * @param redis_client a handle to the fake redis client that sends the request.
   */
  void proxyResponseOnlyStep(const std::string& proxy_response,
                             IntegrationTcpClientPtr& redis_client);

  /**
   * A single step of a larger test involving a fake Redis client and the proxy server.
   * @param request supplies Redis client data to transmit to the proxy.
   * @param proxy_response supplies proxy data in response to the client's request.
   * @param redis_client a handle to the fake redis client that sends the request.
   */
  void proxyResponseStep(const std::string& request, const std::string& proxy_response,
                         IntegrationTcpClientPtr& redis_client);

  /**
   * A single step of a larger test involving a fake Redis client and a specific Redis server.
   * @param upstream a handle to the server that will respond to the request.
   * @param request supplies Redis client data to transmit to the Redis server.
   * @param response supplies Redis server data to transmit to the client.
   * @param redis_client a handle to the fake redis client that sends the request.
   * @param fake_upstream_connection supplies a handle to connection from the proxy to the fake
   * server.
   * @param auth_username supplies the fake upstream's server username, if not an empty string.
   * @param auth_password supplies the fake upstream's server password, if not an empty string.
   */
  void roundtripToUpstreamStep(FakeUpstreamPtr& upstream, const std::string& request,
                               const std::string& response, IntegrationTcpClientPtr& redis_client,
                               FakeRawConnectionPtr& fake_upstream_connection,
                               const std::string& auth_username, const std::string& auth_password,
                               bool stage_auth_ack = false);
  /**
   * A upstream server expects the request on the upstream and respond with the response.
   * @param upstream a handle to the server that will respond to the request.
   * @param request supplies request data sent to the Redis server.
   * @param response supplies Redis server response data to transmit to the client.
   * @param fake_upstream_connection supplies a handle to connection from the proxy to the fake
   * server.
   * @param auth_username supplies the fake upstream's server username, if not an empty string.
   * @param auth_password supplies the fake upstream's server password, if not an empty string.
   * @param stage_auth_ack when true, wait for AUTH bytes only, send +OK, then wait for the
   *   user request bytes. Required by the AWS-IAM init pipeline which gates user requests in
   *   ``held_user_requests_`` until the upstream AUTH ack arrives. The default false matches
   *   the legacy synchronous-AUTH path that ships AUTH and the user request in one burst.
   */
  void expectUpstreamRequestResponse(FakeUpstreamPtr& upstream, const std::string& request,
                                     const std::string& response,
                                     FakeRawConnectionPtr& fake_upstream_connection,
                                     const std::string& auth_username = "",
                                     const std::string& auth_password = "",
                                     bool stage_auth_ack = false);

  /**
   * Similar to ``roundtripToUpstreamStep`` but sends a request and then determines which upstream
   * actually handled it, and responds from that upstream. All the vectors must be the same length.
   * @param fake_upstreams a handle to the servers that may respond to the request.
   * @param request supplies Redis client data to transmit to the Redis server.
   * @param response supplies Redis server data to transmit to the client.
   * @param redis_client a handle to the fake redis client that sends the request.
   * @param fake_upstream_connections supplies a handle to the connections from the proxy to the
   * fake servers. Each entry may be null if a connection has not yet been established.
   * @param auth_username supplies the fake upstreams' server username.
   * @param auth_password supplies the fake upstreams' server password.
   * @param matched_upstream_index output param that indicates which of the upstreams handled the
   * request.
   */
  void roundtripToSomeUpstreamStep(const std::vector<FakeUpstreamPtr>& fake_upstreams,
                                   const std::string& request, const std::string& response,
                                   IntegrationTcpClientPtr& redis_client,
                                   std::vector<FakeRawConnectionPtr>& fake_upstream_connections,
                                   const std::vector<std::string>& auth_usernames,
                                   const std::vector<std::string>& auth_passwords,
                                   std::optional<uint64_t>& matched_upstream_index);

protected:
  const int num_upstreams_;
  const Network::Address::IpVersion version_;
  Runtime::MockLoader* runtime_{};
};

class RedisProxyWithRedirectionIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyWithRedirectionIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_WITH_REDIRECTION, 2) {}

  RedisProxyWithRedirectionIntegrationTest(const std::string& config, int num_upstreams)
      : RedisProxyIntegrationTest(config, num_upstreams) {}

  /**
   * Simple bi-directional test with a fake Redis client and 2 fake Redis servers.
   * @param target_server a handle to the second server that will respond to the request.
   * @param request supplies client data to transmit to the first upstream server.
   * @param redirection_response supplies the moved or ask redirection error from the first server.
   * @param response supplies data sent by the second server back to the fake Redis client.
   * @param asking_response supplies the target_server's response to an "asking" command, if
   * appropriate.
   */
  void simpleRedirection(FakeUpstreamPtr& target_server, const std::string& request,
                         const std::string& redirection_response, const std::string& response,
                         const std::string& asking_response = "+OK\r\n");
};

class RedisProxyWithRedirectionAndDNSIntegrationTest
    : public RedisProxyWithRedirectionIntegrationTest {
public:
  RedisProxyWithRedirectionAndDNSIntegrationTest()
      : RedisProxyWithRedirectionIntegrationTest(
            fmt::format(CONFIG_WITH_REDIRECTION_DNS, CONFIG_WITH_REDIRECTION,
                        Network::Test::ipVersionToDnsFamily(GetParam())),
            2) {}
};

class RedisProxyWithBatchingIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyWithBatchingIntegrationTest() : RedisProxyIntegrationTest(CONFIG_WITH_BATCHING, 2) {}
};

class RedisProxyResp3ListenerIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyResp3ListenerIntegrationTest() : RedisProxyIntegrationTest(CONFIG_RESP3_LISTENER, 2) {}
};

class RedisProxyResp3PreferReplicaIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyResp3PreferReplicaIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_RESP3_PREFER_REPLICA, 2) {}
};

class RedisProxyResp3ListenerWithAuthIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyResp3ListenerWithAuthIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_RESP3_LISTENER_WITH_AUTH, 2) {}
};

class RedisProxyWithRoutesAndAuthPasswordsAwsIamIntegrationTest
    : public Event::TestUsingSimulatedTime,
      public RedisProxyIntegrationTest {
public:
  RedisProxyWithRoutesAndAuthPasswordsAwsIamIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_WITH_ROUTES_AND_AUTH_PASSWORDS_AWS_IAM, 3) {}
};

class RedisProxyWithRoutesAndAuthPasswordsAwsIamNoAuthUsernameIntegrationTest
    : public Event::TestUsingSimulatedTime,
      public RedisProxyIntegrationTest {
public:
  RedisProxyWithRoutesAndAuthPasswordsAwsIamNoAuthUsernameIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_WITH_ROUTES_AND_AUTH_PASSWORDS_AWS_IAM_NO_AUTH_USERNAME,
                                  3) {}
};

class RedisProxyWithRoutesIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyWithRoutesIntegrationTest() : RedisProxyIntegrationTest(CONFIG_WITH_ROUTES, 6) {}
};

class RedisProxyWithDownstreamAuthIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyWithDownstreamAuthIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_WITH_DOWNSTREAM_AUTH_PASSWORD_SET, 2) {}
};

class RedisProxyWithMultipleDownstreamAuthIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyWithMultipleDownstreamAuthIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_WITH_MULTIPLE_DOWNSTREAM_AUTH_PASSWORDS_SET, 2) {}
};

class RedisProxyWithRoutesAndAuthPasswordsIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyWithRoutesAndAuthPasswordsIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_WITH_ROUTES_AND_AUTH_PASSWORDS, 3) {}
};

class RedisProxyWithSeparateAuthPasswordsIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyWithSeparateAuthPasswordsIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_WITH_SEPARATE_AUTH_PASSWORDS, 3) {}
};

class RedisProxyWithMirrorsIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyWithMirrorsIntegrationTest() : RedisProxyIntegrationTest(CONFIG_WITH_MIRROR, 6) {}
};

class RedisProxyResp3WithMirrorsIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyResp3WithMirrorsIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_RESP3_WITH_MIRROR, 6) {}
};

class RedisProxyWithCommandStatsIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyWithCommandStatsIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_WITH_COMMAND_STATS, 2) {}
};

class RedisProxyWithCustomCommandsIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyWithCustomCommandsIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_WITH_CUSTOM_COMMANDS, 2) {}
};

class RedisProxyWithFaultInjectionIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyWithFaultInjectionIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_WITH_FAULT_INJECTION, 2) {}
};

class RedisProxyWithExternalAuthIntegrationTest : public Event::TestUsingSimulatedTime,
                                                  public Grpc::BaseGrpcClientIntegrationParamTest,
                                                  public RedisProxyIntegrationTest {

public:
  RedisProxyWithExternalAuthIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_WITH_EXTERNAL_AUTH, 2) {}

  Grpc::ClientType clientType() const override { return Grpc::ClientType::EnvoyGrpc; }

  Network::Address::IpVersion ipVersion() const override { return GetParam(); }

  void createUpstreams() override {
    RedisProxyIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initialize() override {
    simTime().setSystemTime(std::chrono::seconds(0));

    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto fake_auth_cluster = config_helper_.buildStaticCluster(
          "fake_auth", 0, Network::Test::getLoopbackAddressString(ipVersion()));
      ConfigHelper::setHttp2(fake_auth_cluster);
      *bootstrap.mutable_static_resources()->add_clusters() = fake_auth_cluster;
    });

    RedisProxyIntegrationTest::initialize();
  }

  void expectExternalAuthRequest(FakeHttpConnectionPtr& fake_upstream_connection,
                                 FakeStreamPtr& auth_request, const std::string password,
                                 const bool existing_connection = false) {
    // Connect if not yet.
    if (!existing_connection) {
      auto result =
          fake_upstreams_[2]->waitForHttpConnection(*dispatcher_, fake_upstream_connection);
      RELEASE_ASSERT(result, result.message());
    }

    // Receive data.
    auto result = fake_upstream_connection->waitForNewStream(*dispatcher_, auth_request);
    RELEASE_ASSERT(result, result.message());

    // Get the gRPC message. Ensure it is a RedisProxyExternalAuthRequest.
    envoy::service::redis_auth::v3::RedisProxyExternalAuthRequest received_request;
    result = auth_request->waitForGrpcMessage(*dispatcher_, received_request);
    RELEASE_ASSERT(result, result.message());

    // gRPC request expectations.
    EXPECT_EQ("POST", auth_request->headers().getMethodValue());
    EXPECT_EQ("/envoy.service.redis_auth.v3.RedisProxyExternalAuth/Authenticate",
              auth_request->headers().getPathValue());
    EXPECT_EQ("application/grpc", auth_request->headers().getContentTypeValue());

    // Check that the received password matches the expected password.
    EXPECT_EQ(password, received_request.password());

    // End the request stream.
    result = auth_request->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());
  }

  void sendExternalAuthResponse(FakeStreamPtr& auth_request, const bool authorized,
                                const int64_t expiration_epoch) {
    // Start the stream.
    auth_request->startGrpcStream();

    // Create the response
    envoy::service::redis_auth::v3::RedisProxyExternalAuthResponse response;
    response.mutable_status()->set_code(authorized
                                            ? Grpc::Status::WellKnownGrpcStatus::Ok
                                            : Grpc::Status::WellKnownGrpcStatus::PermissionDenied);
    auto* msg = response.mutable_message();
    *msg = authorized ? "Authorized" : "Unauthorized";
    response.mutable_expiration()->set_seconds(expiration_epoch);

    // Send the response and close stream.
    auth_request->sendGrpcMessage(response);
    auth_request->finishGrpcStream(Grpc::Status::Ok);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithCustomCommandsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithRedirectionIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithRedirectionAndDNSIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithBatchingIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyResp3ListenerIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyResp3ListenerWithAuthIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyResp3PreferReplicaIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyResp3WithMirrorsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// HELLO 3 AUTH <user> <pass> against a RESP3 listener with a downstream password configured:
// the inline AUTH authenticates the connection and the proxy answers locally with its HELLO Map.
// No upstream is involved — auth and HELLO negotiation are handled entirely by the proxy. (Only a
// password is configured, so the Redis 6 ACL synonym ``default`` is the expected username.)
TEST_P(RedisProxyResp3ListenerWithAuthIntegrationTest, Hello3InlineAuthSucceeds) {
  const std::string downstream_hello_reply = resp3HelloMapReply();
  initialize();
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_TRUE(
      redis_client->write(makeBulkStringArray({"hello", "3", "AUTH", "default", "somepassword"})));
  redis_client->waitForData(downstream_hello_reply);
  EXPECT_EQ(downstream_hello_reply, redis_client->data());
  redis_client->close();
}

// HELLO 3 AUTH with the wrong password is rejected ``-WRONGPASS``; the connection is not promoted.
TEST_P(RedisProxyResp3ListenerWithAuthIntegrationTest, Hello3InlineAuthWrongPasswordRejected) {
  const std::string wrongpass = "-WRONGPASS invalid username-password pair\r\n";
  initialize();
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_TRUE(
      redis_client->write(makeBulkStringArray({"hello", "3", "AUTH", "default", "wrongpass"})));
  redis_client->waitForData(wrongpass);
  EXPECT_EQ(wrongpass, redis_client->data());
  redis_client->close();
}

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithRoutesAndAuthPasswordsAwsIamIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions,
                         RedisProxyWithRoutesAndAuthPasswordsAwsIamNoAuthUsernameIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithRoutesIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithDownstreamAuthIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithMultipleDownstreamAuthIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithRoutesAndAuthPasswordsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithSeparateAuthPasswordsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithMirrorsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithCommandStatsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithFaultInjectionIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithExternalAuthIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

void RedisProxyIntegrationTest::initialize() {
  setUpstreamCount(num_upstreams_);
  setDeterministicValue();
  config_helper_.renameListener("redis_proxy");
  BaseIntegrationTest::initialize();
}

void RedisProxyIntegrationTest::roundtripToUpstreamStep(
    FakeUpstreamPtr& upstream, const std::string& request, const std::string& response,
    IntegrationTcpClientPtr& redis_client, FakeRawConnectionPtr& fake_upstream_connection,
    const std::string& auth_username, const std::string& auth_password, bool stage_auth_ack) {
  redis_client->clearData();
  if (fake_upstream_connection.get() != nullptr) {
    fake_upstream_connection->clearData();
  }
  ASSERT_TRUE(redis_client->write(request));

  expectUpstreamRequestResponse(upstream, request, response, fake_upstream_connection,
                                auth_username, auth_password, stage_auth_ack);

  redis_client->waitForData(response);
  // The original response should be received by the fake Redis client.
  EXPECT_EQ(response, redis_client->data());
}

void RedisProxyIntegrationTest::expectUpstreamRequestResponse(
    FakeUpstreamPtr& upstream, const std::string& request, const std::string& response,
    FakeRawConnectionPtr& fake_upstream_connection, const std::string& auth_username,
    const std::string& auth_password, bool stage_auth_ack) {
  std::string proxy_to_server;
  bool expect_auth_command = false;
  std::string ok = "+OK\r\n";

  if (fake_upstream_connection.get() == nullptr) {
    expect_auth_command = (!auth_password.empty());
    EXPECT_TRUE(upstream->waitForRawConnection(fake_upstream_connection));
  }

  if (expect_auth_command) {
    std::string auth_command = (auth_username.empty())
                                   ? makeBulkStringArray({"auth", auth_password})
                                   : makeBulkStringArray({"auth", auth_username, auth_password});
    if (stage_auth_ack) {
      // AWS-IAM init pipeline: AUTH lands first, the upstream client holds the user request
      // in held_user_requests_ until +OK arrives, then drains. waitForData waits for an exact
      // byte count, so we MUST split the wait into two phases — combining AUTH + request in a
      // single waitForData would never see the buffer settle at the combined size before +OK
      // is written, and after +OK it would never see exactly auth.size() either.
      EXPECT_TRUE(fake_upstream_connection->waitForData(auth_command.size(), &proxy_to_server));
      EXPECT_EQ(auth_command, proxy_to_server);
      EXPECT_TRUE(fake_upstream_connection->write(ok));
      EXPECT_TRUE(fake_upstream_connection->waitForData(auth_command.size() + request.size(),
                                                        &proxy_to_server));
      EXPECT_EQ(auth_command + request, proxy_to_server);
    } else {
      // Legacy synchronous-AUTH path: the upstream client snaps to Ready inline and ships
      // AUTH and the user request together in a single burst, so a single waitForData on
      // the combined size is the correct expectation.
      EXPECT_TRUE(fake_upstream_connection->waitForData(auth_command.size() + request.size(),
                                                        &proxy_to_server));
      EXPECT_EQ(auth_command + request, proxy_to_server);
      EXPECT_TRUE(fake_upstream_connection->write(ok));
    }
  } else {
    EXPECT_TRUE(fake_upstream_connection->waitForData(request.size(), &proxy_to_server));
    // The original request should be the same as the data received by the server.
    EXPECT_EQ(request, proxy_to_server);
  }

  EXPECT_TRUE(fake_upstream_connection->write(response));
}

void RedisProxyIntegrationTest::roundtripToSomeUpstreamStep(
    const std::vector<FakeUpstreamPtr>& fake_upstreams, const std::string& request,
    const std::string& response, IntegrationTcpClientPtr& redis_client,
    std::vector<FakeRawConnectionPtr>& fake_upstream_connections,
    const std::vector<std::string>& auth_usernames, const std::vector<std::string>& auth_passwords,
    std::optional<uint64_t>& matched_upstream_index) {
  redis_client->clearData();
  for (auto& fake_upstream_connection : fake_upstream_connections) {
    if (fake_upstream_connection != nullptr) {
      fake_upstream_connection->clearData();
    }
  }

  ASSERT_TRUE(redis_client->write(request));

  Event::TestTimeSystem::RealTimeBound bound(TestUtility::DefaultTimeout);
  while (bound.withinBound()) {
    for (uint64_t i = 0; i < fake_upstreams.size(); ++i) {
      std::string proxy_to_server;
      if (fake_upstream_connections[i] == nullptr) {
        if (auto result = fake_upstreams[i]->waitForRawConnection(fake_upstream_connections[i],
                                                                  std::chrono::milliseconds(5));
            !result) {
          continue;
        }
        std::string auth_command =
            makeBulkStringArray({"auth", auth_usernames[i], auth_passwords[i]});
        if (auto result = fake_upstream_connections[i]->waitForData(
                auth_command.size() + request.size(), &proxy_to_server,
                std::chrono::milliseconds(5));
            !result) {
          continue;
        }
        EXPECT_EQ(auth_command + request, proxy_to_server);
        const std::string ok = "+OK\r\n";
        EXPECT_TRUE(fake_upstream_connections[i]->write(ok));
      } else {
        if (auto result = fake_upstream_connections[i]->waitForData(
                request.size(), &proxy_to_server, std::chrono::milliseconds(5));
            !result) {
          continue;
        }
        EXPECT_EQ(request, proxy_to_server);
      }
      EXPECT_TRUE(fake_upstream_connections[i]->write(response));
      redis_client->waitForData(response);
      EXPECT_EQ(response, redis_client->data());
      matched_upstream_index = i;
      return;
    }
  }
}

void RedisProxyIntegrationTest::simpleRoundtripToUpstream(FakeUpstreamPtr& upstream,
                                                          const std::string& request,
                                                          const std::string& response) {
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;

  roundtripToUpstreamStep(upstream, request, response, redis_client, fake_upstream_connection, "",
                          "");

  EXPECT_TRUE(fake_upstream_connection->close());
  redis_client->close();
}

void RedisProxyIntegrationTest::proxyRequestStep(const std::string& request,
                                                 IntegrationTcpClientPtr& redis_client) {
  redis_client->clearData();
  ASSERT_TRUE(redis_client->write(request));
}

void RedisProxyIntegrationTest::proxyResponseOnlyStep(const std::string& proxy_response,
                                                      IntegrationTcpClientPtr& redis_client) {
  redis_client->waitForData(proxy_response);
  // After sending the request to the proxy, the fake redis client should receive proxy_response.
  EXPECT_EQ(proxy_response, redis_client->data());
}

void RedisProxyIntegrationTest::proxyResponseStep(const std::string& request,
                                                  const std::string& proxy_response,
                                                  IntegrationTcpClientPtr& redis_client) {
  proxyRequestStep(request, redis_client);
  proxyResponseOnlyStep(proxy_response, redis_client);
}

void RedisProxyIntegrationTest::simpleProxyResponse(const std::string& request,
                                                    const std::string& proxy_response) {
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  proxyResponseStep(request, proxy_response, redis_client);
  redis_client->close();
}

void RedisProxyWithRedirectionIntegrationTest::simpleRedirection(
    FakeUpstreamPtr& target_server, const std::string& request,
    const std::string& redirection_response, const std::string& response,
    const std::string& asking_response) {

  bool asking = (redirection_response.find("-ASK") != std::string::npos);
  std::string proxy_to_server;
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_TRUE(redis_client->write(request));

  FakeRawConnectionPtr fake_upstream_connection_1, fake_upstream_connection_2;

  // Data from the client should always be routed to fake_upstreams_[0] by the load balancer.
  EXPECT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_1));
  EXPECT_TRUE(fake_upstream_connection_1->waitForData(request.size(), &proxy_to_server));
  // The data in request should be received by the first server, fake_upstreams_[0].
  EXPECT_EQ(request, proxy_to_server);
  proxy_to_server.clear();

  // Send the redirection_response from the first fake Redis server back to the proxy.
  EXPECT_TRUE(fake_upstream_connection_1->write(redirection_response));
  // The proxy should initiate a new connection to the fake redis server, target_server, in
  // response.
  EXPECT_TRUE(target_server->waitForRawConnection(fake_upstream_connection_2));

  if (asking) {
    // The server, target_server, should receive an "asking" command before the original request.
    std::string asking_request = makeBulkStringArray({"asking"});
    EXPECT_TRUE(fake_upstream_connection_2->waitForData(asking_request.size() + request.size(),
                                                        &proxy_to_server));
    EXPECT_EQ(asking_request + request, proxy_to_server);
    // Respond to the "asking" command.
    EXPECT_TRUE(fake_upstream_connection_2->write(asking_response));
  } else {
    // The server, target_server, should receive request unchanged.
    EXPECT_TRUE(fake_upstream_connection_2->waitForData(request.size(), &proxy_to_server));
    EXPECT_EQ(request, proxy_to_server);
  }

  // Send response from the second fake Redis server, target_server, to the client.
  EXPECT_TRUE(fake_upstream_connection_2->write(response));
  redis_client->waitForData(response);
  // The client should receive response unchanged.
  EXPECT_EQ(response, redis_client->data());

  EXPECT_TRUE(fake_upstream_connection_1->close());
  EXPECT_TRUE(fake_upstream_connection_2->close());
  redis_client->close();
}

// This test sends a simple "get foo" command from a fake
// downstream client through the proxy to a fake upstream
// Redis server. The fake server sends a valid response
// back to the client. The request and response should
// make it through the envoy proxy server code unchanged.

TEST_P(RedisProxyIntegrationTest, SimpleRequestAndResponse) {
  initialize();
  simpleRequestAndResponse(makeBulkStringArray({"get", "foo"}), "$3\r\nbar\r\n");
}

TEST_P(RedisProxyWithCommandStatsIntegrationTest, MGETRequestAndResponse) {
  initialize();
  std::string request = makeBulkStringArray({"mget", "foo"});
  std::string upstream_response = "$3\r\nbar\r\n";
  std::string downstream_response =
      "*1\r\n" + upstream_response; // Downstream response is array of length 1

  // Make MGET request from downstream
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  redis_client->clearData();
  ASSERT_TRUE(redis_client->write(request));

  // Make GET request to upstream (MGET is turned into GETs for upstream)
  FakeUpstreamPtr& upstream = fake_upstreams_[0];
  FakeRawConnectionPtr fake_upstream_connection;
  std::string auth_username = "";
  std::string auth_password = "";
  std::string upstream_request = makeBulkStringArray({"get", "foo"});
  expectUpstreamRequestResponse(upstream, upstream_request, upstream_response,
                                fake_upstream_connection, auth_username, auth_password);

  // Downstream response for MGET
  redis_client->waitForData(downstream_response);
  EXPECT_EQ(downstream_response, redis_client->data());

  // Cleanup
  EXPECT_TRUE(fake_upstream_connection->close());
  redis_client->close();
}

TEST_P(RedisProxyIntegrationTest, KEYSRequestAndResponse) {
  initialize();
  std::string request = makeBulkStringArray({"keys", "*"});
  std::string upstream_response = "*2\r\n$4\r\nbar1\r\n$4\r\nbar2\r\n";
  const std::string& downstream_response = upstream_response;

  // Make KEYS request from downstream
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  redis_client->clearData();
  ASSERT_TRUE(redis_client->write(request));

  // Make KEYS request to upstream
  FakeUpstreamPtr& upstream = fake_upstreams_[0];
  FakeRawConnectionPtr fake_upstream_connection;
  std::string auth_username = "";
  std::string auth_password = "";
  std::string upstream_request = makeBulkStringArray({"keys", "*"});
  expectUpstreamRequestResponse(upstream, upstream_request, upstream_response,
                                fake_upstream_connection, auth_username, auth_password);

  // Downstream response for KEYS
  redis_client->waitForData(downstream_response);
  EXPECT_EQ(downstream_response, redis_client->data());

  // Cleanup
  EXPECT_TRUE(fake_upstream_connection->close());
  redis_client->close();
}

TEST_P(RedisProxyIntegrationTest, QUITRequestAndResponse) {
  initialize();
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_TRUE(redis_client->write(makeBulkStringArray({"quit"}), false, false));
  redis_client->waitForData("+OK\r\n");
  redis_client->waitForDisconnect();
  EXPECT_EQ(redis_client->data(), "+OK\r\n");
  redis_client->close();
}

// This test sends an invalid Redis command from a fake
// downstream client to the envoy proxy. Envoy will respond
// with an ERR unknown command error.

TEST_P(RedisProxyIntegrationTest, UnknownCommand) {
  std::stringstream error_response;
  error_response << "-"
                 << "ERR unknown command 'foo', with args beginning with: "
                 << "\r\n";
  initialize();
  simpleProxyResponse(makeBulkStringArray({"foo"}), error_response.str());
}

// This test sends an invalid Redis command from a fake
// downstream client to the envoy proxy. Envoy will respond
// with an ERR unknown command error.

TEST_P(RedisProxyIntegrationTest, UnknownCommandWithArgs) {
  std::stringstream error_response;
  error_response << "-"
                 << "ERR unknown command 'unknowncmd', with args beginning with: world"
                 << "\r\n";
  initialize();
  simpleProxyResponse(makeBulkStringArray({"unknowncmd", "world"}), error_response.str());
}

// This test sends an invalid Redis command from a fake
// downstream client to the envoy proxy. Envoy will respond
// with an ERR unknown command error.

TEST_P(RedisProxyIntegrationTest, HelloCommand) {
  // Test HELLO command with invalid protocol version argument
  std::stringstream error_response;
  error_response << "-"
                 << "NOPROTO unsupported protocol version"
                 << "\r\n";
  initialize();
  simpleProxyResponse(makeBulkStringArray({"hello", "world"}), error_response.str());
}

// RESP2 listener (the default) rejects explicit ``HELLO 3`` with ``-NOPROTO`` — the proxy
// only allows the listener-configured version. simpleProxyResponse confirms no upstream
// connection is created since the response is generated locally before any dispatch.
TEST_P(RedisProxyIntegrationTest, Resp2ListenerRejectsHello3) {
  const std::string error_response = "-NOPROTO unsupported protocol version\r\n";
  initialize();
  simpleProxyResponse(makeBulkStringArray({"hello", "3"}), error_response);
}

// RESP2 listener, HELLO 2: the proxy builds the HELLO reply as a RESP3 Map internally, then the
// encoder DOWN-CONVERTS it to a flat RESP2 array for the RESP2-negotiated downstream — the ``%7``
// Map becomes ``*14`` with the seven key/value pairs flattened (proto is reported as ``:2``). Pins
// the Map->array RESP2 down-conversion end-to-end on the local HELLO reply path.
TEST_P(RedisProxyIntegrationTest, Resp2ListenerHello2RepliesFlatArray) {
  const std::string hello2_flat_reply = "*14\r\n"
                                        "$6\r\nserver\r\n$17\r\nenvoy-redis-proxy\r\n"
                                        "$7\r\nversion\r\n$5\r\n6.0.0\r\n"
                                        "$5\r\nproto\r\n:2\r\n"
                                        "$2\r\nid\r\n:0\r\n"
                                        "$4\r\nmode\r\n$10\r\nstandalone\r\n"
                                        "$4\r\nrole\r\n$6\r\nmaster\r\n"
                                        "$7\r\nmodules\r\n*0\r\n";
  initialize();
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_TRUE(redis_client->write(makeBulkStringArray({"hello", "2"})));
  redis_client->waitForData(hello2_flat_reply);
  EXPECT_EQ(hello2_flat_reply, redis_client->data());
  redis_client->close();
}

// RESP3 listener rejects any data command sent before a successful ``HELLO 3`` handshake
// with ``-NOPROTO``. The gate runs in ``ProxyFilter::processRespValue`` before reaching the
// splitter or conn pool, so the response is generated locally. The proxy-filter unit suite
// (``RedisProxyFilterResp3Test.GetBeforeHelloRejectedNoproto``) asserts ``Times(0)`` on the
// splitter to pin "no upstream dispatch"; this integration covers the wire-level error
// shape.
TEST_P(RedisProxyResp3ListenerIntegrationTest, GetBeforeHelloRejectedNoproto) {
  const std::string error_response = "-NOPROTO unsupported protocol version\r\n";
  initialize();
  simpleProxyResponse(makeBulkStringArray({"get", "foo"}), error_response);
  // The pre-HELLO gate increments ``downstream_rq_noproto`` as it rejects the data command. The
  // unit suite covers the gate logic; this pins the wired stat end-to-end.
  test_server_->waitForCounter("redis.redis_stats.downstream_rq_noproto", testing::Eq(1));
}

// RESP3 listener positive path — full HELLO 3 + GET round trip:
//   * downstream HELLO 3 → proxy answers locally with the HELLO Map (proto=3);
//   * downstream GET foo → proxy opens an upstream connection and sends HELLO 3 first
//     (the upstream init pipeline);
//   * upstream replies with a HELLO Map containing proto=3, which the proxy's
//     ``isHello3SuccessResponse`` check accepts;
//   * proxy then ships the held GET foo to upstream;
//   * upstream replies ``$5\r\nhello\r\n`` and the proxy forwards it to the downstream client.
// The proxy HELLO Map bytes mirror ``buildHelloReply(3)`` in command_splitter_impl.cc; the
// upstream HELLO Map only needs ``proto:3`` to be accepted by Hello3InitCallbacks.
TEST_P(RedisProxyResp3ListenerIntegrationTest,
       Hello3ThenGetFullRoundtripWithUpstreamHelloNegotiation) {
  initialize();

  // Proxy's local HELLO 3 reply (see resp3HelloMapReply / buildHelloReply).
  const std::string downstream_hello_reply = resp3HelloMapReply();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));

  // Step 1: downstream HELLO 3 -> proxy local Map reply.
  ASSERT_TRUE(redis_client->write(makeBulkStringArray({"hello", "3"})));
  redis_client->waitForData(downstream_hello_reply);
  EXPECT_EQ(downstream_hello_reply, redis_client->data());
  redis_client->clearData();

  // Step 2: downstream GET foo -> opens upstream, which the proxy first hits with HELLO 3.
  const std::string get_request = makeBulkStringArray({"get", "foo"});
  ASSERT_TRUE(redis_client->write(get_request));

  // Stage A: upstream sees HELLO 3 first (held-user-request gate behind HELLO).
  const std::string upstream_hello_request = makeBulkStringArray({"HELLO", "3"});
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string proxy_to_server;
  EXPECT_TRUE(
      fake_upstream_connection->waitForData(upstream_hello_request.size(), &proxy_to_server));
  EXPECT_EQ(upstream_hello_request, proxy_to_server);

  // Minimal valid HELLO 3 reply: a single-pair Map with proto=3 (proxy's
  // isHello3SuccessResponse only looks for that key).
  const std::string upstream_hello_reply = "%1\r\n$5\r\nproto\r\n:3\r\n";
  EXPECT_TRUE(fake_upstream_connection->write(upstream_hello_reply));

  // Stage B: held GET foo flushes to upstream after HELLO 3 succeeds.
  proxy_to_server.clear();
  EXPECT_TRUE(fake_upstream_connection->waitForData(
      upstream_hello_request.size() + get_request.size(), &proxy_to_server));
  EXPECT_EQ(upstream_hello_request + get_request, proxy_to_server);

  // Stage C: upstream replies; proxy forwards to client.
  const std::string get_reply = "$5\r\nhello\r\n";
  EXPECT_TRUE(fake_upstream_connection->write(get_reply));
  redis_client->waitForData(get_reply);
  EXPECT_EQ(get_reply, redis_client->data());

  EXPECT_TRUE(fake_upstream_connection->close());
  redis_client->close();
}

// RESP3 typed-payload fidelity: after HELLO 3 negotiation, upstream replies carrying RESP3
// Double, Map, and Set frames must each reach the downstream client byte-identical. The proxy
// decodes and re-encodes in RESP3 (no cross-version down-conversion), and the Double payload
// bytes are preserved verbatim. Pins the end-to-end fidelity the codec unit tests assert in
// isolation.
TEST_P(RedisProxyResp3ListenerIntegrationTest, Resp3TypedPayloadPassthrough) {
  initialize();

  // Full proxy-local HELLO 3 reply (see Hello3ThenGetFullRoundtrip... above). Wait for ALL of
  // it before clearing so trailing bytes cannot bleed into the first upstream reply assertion.
  const std::string downstream_hello_reply = resp3HelloMapReply();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));

  // Negotiate downstream RESP3 (consume the proxy's full local HELLO Map reply).
  ASSERT_TRUE(redis_client->write(makeBulkStringArray({"hello", "3"})));
  redis_client->waitForData(downstream_hello_reply);
  redis_client->clearData();

  // First command opens the upstream and triggers the proxy's upstream HELLO 3 negotiation.
  const std::string upstream_hello_request = makeBulkStringArray({"HELLO", "3"});
  const std::string zscore_request = makeBulkStringArray({"zscore", "k", "m"});
  ASSERT_TRUE(redis_client->write(zscore_request));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string proxy_to_server;
  // Wait for the proxy's upstream HELLO 3 to land BEFORE replying — the fake raw connection
  // accumulates received bytes and matches them FIFO, so writing a reply before the request has
  // been forwarded would race that matching. Every wait below targets the cumulative byte total.
  EXPECT_TRUE(
      fake_upstream_connection->waitForData(upstream_hello_request.size(), &proxy_to_server));
  EXPECT_EQ(upstream_hello_request, proxy_to_server);
  // HELLO 3 answered: this unblocks the held ``ZSCORE``, which now flushes to the upstream.
  EXPECT_TRUE(fake_upstream_connection->write("%1\r\n$5\r\nproto\r\n:3\r\n"));
  std::string expected_upstream = upstream_hello_request + zscore_request;
  EXPECT_TRUE(fake_upstream_connection->waitForData(expected_upstream.size(), &proxy_to_server));
  EXPECT_EQ(expected_upstream, proxy_to_server);

  // For each typed RESP3 frame: upstream writes it, the downstream client must receive the
  // exact bytes. The Double uses a non-canonical form with a trailing zero (``3.140``) that a
  // parse-then-reformat round-trip would collapse to ``3.14`` — preserving it proves the proxy
  // ships the raw payload through unchanged. Each request/reply is fully drained before the next
  // so the assertions stay deterministic.
  auto roundtrip = [&](const std::string& request, const std::string& reply) {
    if (!request.empty()) {
      ASSERT_TRUE(redis_client->write(request));
      expected_upstream += request;
      EXPECT_TRUE(
          fake_upstream_connection->waitForData(expected_upstream.size(), &proxy_to_server));
      EXPECT_EQ(expected_upstream, proxy_to_server);
    }
    EXPECT_TRUE(fake_upstream_connection->write(reply));
    redis_client->waitForData(reply);
    EXPECT_EQ(reply, redis_client->data());
    redis_client->clearData();
  };

  // All three commands are issued over the single upstream connection the test drives above, so
  // the cumulative per-connection byte assertions stay deterministic.
  //
  // Double reply to the already-dispatched ``ZSCORE`` (request empty — already on the wire).
  roundtrip("", ",3.140\r\n");
  // Map reply (``HGETALL``).
  roundtrip(makeBulkStringArray({"hgetall", "k"}), "%1\r\n$1\r\nf\r\n$1\r\nv\r\n");
  // Set reply (``SMEMBERS``).
  roundtrip(makeBulkStringArray({"smembers", "k"}), "~2\r\n$1\r\na\r\n$1\r\nb\r\n");

  EXPECT_TRUE(fake_upstream_connection->close());
  redis_client->close();
}

// Upstream HELLO 3 failure surfaces as an ``upstream_resp3_hello_failure`` stat increment. On a
// RESP3 listener every routed upstream connection negotiates ``HELLO 3``; an upstream that
// answers with ``proto`` != 3 (here a Redis 6-style ``proto:2`` Map) fails negotiation. Docs and
// the proto comment describe this stat as the operational signal for a misconfigured upstream,
// so pin it end-to-end.
TEST_P(RedisProxyResp3ListenerIntegrationTest, UpstreamHello3FailureIncrementsStat) {
  initialize();

  // Full proxy-local HELLO 3 reply — wait for ALL of it before clearing so trailing bytes
  // cannot bleed into the held GET's error reply assertion below.
  const std::string downstream_hello_reply = resp3HelloMapReply();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));

  // Negotiate downstream RESP3 (the proxy answers HELLO 3 locally with its Map).
  ASSERT_TRUE(redis_client->write(makeBulkStringArray({"hello", "3"})));
  redis_client->waitForData(downstream_hello_reply);
  redis_client->clearData();

  // A data command opens the upstream; the proxy sends HELLO 3 first.
  const std::string upstream_hello_request = makeBulkStringArray({"HELLO", "3"});
  ASSERT_TRUE(redis_client->write(makeBulkStringArray({"get", "k"})));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string proxy_to_server;
  EXPECT_TRUE(
      fake_upstream_connection->waitForData(upstream_hello_request.size(), &proxy_to_server));
  EXPECT_EQ(upstream_hello_request, proxy_to_server);

  // Upstream answers HELLO 3 with proto=2 — negotiation fails. The proxy then, in order:
  //   (1) increments upstream_resp3_hello_failure,
  //   (2) fails the held GET downstream with ``-upstream failure``,
  //   (3) closes the failed upstream connection (ClientImpl::onInitFailure -> connection close).
  EXPECT_TRUE(fake_upstream_connection->write("%1\r\n$5\r\nproto\r\n:2\r\n"));

  // (1) The failure counter must increment.
  test_server_->waitForCounter("cluster.cluster_0.redis_cluster.upstream_resp3_hello_failure",
                               testing::Ge(1));

  // (2) Downstream-visible behavior: the held GET fails — the proxy returns ``-upstream failure``.
  const std::string upstream_failure_reply = "-upstream failure\r\n";
  redis_client->waitForData(upstream_failure_reply);
  EXPECT_EQ(upstream_failure_reply, redis_client->data());

  // (3) Wait for the proxy to tear the failed upstream connection down, making the teardown
  // ordering explicit instead of leaving the connection dangling until test exit.
  EXPECT_TRUE(fake_upstream_connection->waitForDisconnect());

  redis_client->close();
}

// RESP3 server-initiated Push frames (``>N``) from the upstream are not replies; the proxy's
// upstream client drops them (ClientImpl::onRespValue) without forwarding to the downstream or
// disturbing the request pipeline. Wire-level companion to the client_impl_test Push unit tests:
// an upstream Push interleaved before a GET reply must not reach the client, and the GET reply
// must still arrive intact.
TEST_P(RedisProxyResp3ListenerIntegrationTest, UpstreamPushFrameDroppedBeforeReply) {
  initialize();

  const std::string downstream_hello_reply = resp3HelloMapReply();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));

  // Negotiate downstream RESP3.
  ASSERT_TRUE(redis_client->write(makeBulkStringArray({"hello", "3"})));
  redis_client->waitForData(downstream_hello_reply);
  redis_client->clearData();

  // GET opens the upstream; the proxy negotiates HELLO 3 first.
  const std::string upstream_hello_request = makeBulkStringArray({"HELLO", "3"});
  const std::string get_request = makeBulkStringArray({"get", "k"});
  ASSERT_TRUE(redis_client->write(get_request));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string proxy_to_server;
  EXPECT_TRUE(
      fake_upstream_connection->waitForData(upstream_hello_request.size(), &proxy_to_server));
  EXPECT_EQ(upstream_hello_request, proxy_to_server);
  EXPECT_TRUE(fake_upstream_connection->write("%1\r\n$5\r\nproto\r\n:3\r\n"));

  // The held GET flushes once HELLO 3 succeeds.
  proxy_to_server.clear();
  EXPECT_TRUE(fake_upstream_connection->waitForData(
      upstream_hello_request.size() + get_request.size(), &proxy_to_server));
  EXPECT_EQ(upstream_hello_request + get_request, proxy_to_server);

  // Upstream emits a server-initiated Push frame and THEN the real GET reply in a single write.
  // The proxy must drop the Push and forward only the GET reply.
  const std::string get_reply = "$5\r\nhello\r\n";
  EXPECT_TRUE(fake_upstream_connection->write(">1\r\n$4\r\nping\r\n" + get_reply));

  // Downstream sees exactly the GET reply — the Push never arrives, and the FIFO is intact.
  redis_client->waitForData(get_reply);
  EXPECT_EQ(get_reply, redis_client->data());

  EXPECT_TRUE(fake_upstream_connection->close());
  redis_client->close();
}

// RESP3 listener also rejects bare ``HELLO`` (no version argument) on a fresh connection.
// Bare HELLO inherits the connection's current downstream RESP version (default 2 on a fresh
// connection); the exact-match check against the listener's required RESP3 fails. After a
// successful ``HELLO 3`` the connection would be on version 3 and bare HELLO would be
// accepted — covered by the splitter unit tests.
TEST_P(RedisProxyResp3ListenerIntegrationTest, BareHelloOnFreshConnectionRejectedNoproto) {
  const std::string error_response = "-NOPROTO unsupported protocol version\r\n";
  initialize();
  simpleProxyResponse(makeBulkStringArray({"hello"}), error_response);
}

// RESP3 listener rejects an explicit ``HELLO 2`` (and any version that is not the listener's
// required version) with ``-NOPROTO``. Symmetric to the RESP2-listener-rejects-HELLO3 test.
TEST_P(RedisProxyResp3ListenerIntegrationTest, Hello2OnResp3ListenerRejectedNoproto) {
  const std::string error_response = "-NOPROTO unsupported protocol version\r\n";
  initialize();
  simpleProxyResponse(makeBulkStringArray({"hello", "2"}), error_response);
}

// RESP3 + non-Primary read policy: a routed upstream must negotiate HELLO 3, then READONLY,
// before the held user command flushes. Pins the wire-level init order (HELLO 3 -> READONLY ->
// GET); the unit test covers only the state machine.
TEST_P(RedisProxyResp3PreferReplicaIntegrationTest,
       Resp3NonPrimaryReadPolicySendsReadonlyBeforeUserCommand) {
  initialize();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_NO_FATAL_FAILURE(negotiateDownstreamResp3(redis_client));

  // The first data command opens the upstream and is held behind init.
  ASSERT_TRUE(redis_client->write(makeBulkStringArray({"get", "k"})));

  FakeRawConnectionPtr conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(conn));

  std::string seen;
  std::string expected;
  ASSERT_NO_FATAL_FAILURE(expectUpstreamHello3AndReply(conn, expected));

  // READONLY follows HELLO 3 success. The GET must NOT be on the wire yet — the exact-size wait
  // would fail if it had raced ahead of READONLY.
  expected += makeBulkStringArray({"readonly"});
  ASSERT_TRUE(conn->waitForData(expected.size(), &seen));
  ASSERT_EQ(expected, seen);
  ASSERT_TRUE(conn->write("+OK\r\n"));

  // READONLY acked: the held GET flushes.
  expected += makeBulkStringArray({"get", "k"});
  ASSERT_TRUE(conn->waitForData(expected.size(), &seen));
  ASSERT_EQ(expected, seen);
  ASSERT_TRUE(conn->write("$3\r\nbar\r\n"));

  redis_client->waitForData("$3\r\nbar\r\n");
  EXPECT_EQ("$3\r\nbar\r\n", redis_client->data());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.redis_cluster.upstream_resp3_hello_failure")
                   ->value());

  EXPECT_TRUE(conn->close());
  redis_client->close();
}

// RESP3 + transaction: the dedicated transaction client must negotiate HELLO 3 with the upstream
// before MULTI...EXEC reaches Redis. The conn-pool unit test confirms the transaction path is
// constructed with Resp3; this pins the real wire ordering end-to-end.
TEST_P(RedisProxyResp3ListenerIntegrationTest, Resp3TransactionNegotiatesHello3BeforeMultiExec) {
  initialize();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_NO_FATAL_FAILURE(negotiateDownstreamResp3(redis_client));

  const std::string txn = makeBulkStringArray({"MULTI"}) + makeBulkStringArray({"set", "k", "v"}) +
                          makeBulkStringArray({"get", "k"}) + makeBulkStringArray({"exec"});
  // A value, never a const-ref bound to a temporary.
  const std::string response = "+OK\r\n+QUEUED\r\n+QUEUED\r\n*2\r\n+OK\r\n$1\r\nv\r\n";
  ASSERT_TRUE(redis_client->write(txn));

  FakeRawConnectionPtr conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(conn));

  std::string seen;
  std::string expected;
  // Only HELLO 3 is on the wire so far — no transaction command precedes negotiation success.
  ASSERT_NO_FATAL_FAILURE(expectUpstreamHello3AndReply(conn, expected));

  // HELLO 3 acked: the held MULTI...EXEC flush in order on the same upstream connection.
  expected += txn;
  ASSERT_TRUE(conn->waitForData(expected.size(), &seen));
  ASSERT_EQ(expected, seen);
  ASSERT_TRUE(conn->write(response));

  redis_client->waitForData(response);
  EXPECT_EQ(response, redis_client->data());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.redis_cluster.upstream_resp3_hello_failure")
                   ->value());

  EXPECT_TRUE(conn->close());
  redis_client->close();
}

// RESP3 + transaction HELLO 3 FAILURE: the dedicated transaction client owns its upstream
// connection, a different ownership path than the pooled-client failure covered by
// UpstreamHello3FailureIncrementsStat. When that client's HELLO 3 is answered with proto=2,
// negotiation must fail cleanly: the failure counter increments, the held MULTI...EXEC is failed
// downstream, and the transaction client's connection is torn down (no half-open transaction).
TEST_P(RedisProxyResp3ListenerIntegrationTest, Resp3TransactionHello3FailureFailsHeldMultiExec) {
  initialize();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_NO_FATAL_FAILURE(negotiateDownstreamResp3(redis_client));

  const std::string txn = makeBulkStringArray({"MULTI"}) + makeBulkStringArray({"set", "k", "v"}) +
                          makeBulkStringArray({"exec"});
  ASSERT_TRUE(redis_client->write(txn));

  // The transaction opens a dedicated upstream connection; HELLO 3 is sent before any MULTI byte.
  FakeRawConnectionPtr conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(conn));
  const std::string upstream_hello = makeBulkStringArray({"HELLO", "3"});
  std::string seen;
  ASSERT_TRUE(conn->waitForData(upstream_hello.size(), &seen));
  ASSERT_EQ(upstream_hello, seen);

  // Answer HELLO 3 with proto=2 → negotiation fails on the transaction client.
  ASSERT_TRUE(conn->write("%1\r\n$5\r\nproto\r\n:2\r\n"));

  // The failure counter increments and the held transaction fails downstream rather than
  // reaching Redis (a half-open transaction must not survive). MULTI was already answered
  // locally with +OK to open the transaction; the queued SET and the EXEC then each fail with
  // ``-upstream failure`` when the transaction client's HELLO 3 negotiation fails — so the
  // full downstream reply is the local MULTI ack followed by two upstream-failure errors.
  test_server_->waitForCounter("cluster.cluster_0.redis_cluster.upstream_resp3_hello_failure",
                               testing::Ge(1));
  const std::string expected_reply = "+OK\r\n-upstream failure\r\n-upstream failure\r\n";
  redis_client->waitForData(expected_reply);
  EXPECT_EQ(expected_reply, redis_client->data());

  // The transaction client's connection is torn down; no MULTI/SET/EXEC byte ever reached the
  // upstream (only HELLO 3 did).
  EXPECT_TRUE(conn->waitForDisconnect());

  redis_client->close();
}
// RESP3 + mirror: the filter-level protocol_version applies to the mirror conn pools too, so the
// primary AND every mirror upstream negotiate HELLO 3 before the mirrored write. Pins that a
// mirror does not silently stay on RESP2 and that the mirrored SET never precedes its HELLO 3.
TEST_P(RedisProxyResp3WithMirrorsIntegrationTest,
       Resp3MirroredWriteNegotiatesHello3OnPrimaryAndMirror) {
  initialize();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_NO_FATAL_FAILURE(negotiateDownstreamResp3(redis_client));

  const std::string set = makeBulkStringArray({"set", "mirror_key", "value"});
  ASSERT_TRUE(redis_client->write(set));

  // Primary cluster_0 ([0]) and mirror clusters cluster_1 ([2]) / cluster_2 ([4]) each open their
  // own upstream and independently negotiate HELLO 3 before the SET. Distinct mirror replies make
  // the "mirror reply is ignored" assertion meaningful.
  std::array<FakeRawConnectionPtr, 3> conns;
  auto negotiate_leg = [&](size_t slot, int upstream_index, const std::string& reply) {
    ASSERT_TRUE(fake_upstreams_[upstream_index]->waitForRawConnection(conns[slot]));
    std::string seen;
    std::string expected;
    ASSERT_NO_FATAL_FAILURE(expectUpstreamHello3AndReply(conns[slot], expected));
    expected += set; // the mirrored SET must not appear before this leg's HELLO 3.
    ASSERT_TRUE(conns[slot]->waitForData(expected.size(), &seen));
    ASSERT_EQ(expected, seen);
    ASSERT_TRUE(conns[slot]->write(reply));
  };
  ASSERT_NO_FATAL_FAILURE(negotiate_leg(0, 0, "+OK\r\n"));        // primary cluster_0
  ASSERT_NO_FATAL_FAILURE(negotiate_leg(1, 2, "$4\r\nbar1\r\n")); // mirror cluster_1 (ignored)
  ASSERT_NO_FATAL_FAILURE(negotiate_leg(2, 4, "$4\r\nbar2\r\n")); // mirror cluster_2 (ignored)

  // Only the primary's reply reaches downstream.
  redis_client->waitForData("+OK\r\n");
  EXPECT_EQ("+OK\r\n", redis_client->data());

  // HELLO 3 succeeded on every conn pool (primary + both mirrors).
  for (const char* stat : {
           "cluster.cluster_0.redis_cluster.upstream_resp3_hello_failure",
           "cluster.cluster_1.redis_cluster.upstream_resp3_hello_failure",
           "cluster.cluster_2.redis_cluster.upstream_resp3_hello_failure",
       }) {
    EXPECT_EQ(0, test_server_->counter(stat)->value());
  }

  for (auto& conn : conns) {
    EXPECT_TRUE(conn->close());
  }
  redis_client->close();
}

// This test sends an invalid Redis command from a fake
// downstream client to the envoy proxy. Envoy will respond
// with an invalid request error.

TEST_P(RedisProxyIntegrationTest, InvalidRequest) {
  std::stringstream error_response;
  error_response << "-" << RedisCmdSplitter::Response::get().InvalidRequest << "\r\n";
  initialize();
  simpleProxyResponse(makeBulkStringArray({"keys"}), error_response.str());
}

// This test sends a simple Redis command to a fake upstream
// Redis server. The server replies with a MOVED or ASK redirection
// error, and that error is passed unchanged to the fake downstream
// since redirection support has not been enabled (by default).

TEST_P(RedisProxyIntegrationTest, RedirectWhenNotEnabled) {
  std::string request = makeBulkStringArray({"get", "foo"});
  initialize();
  if (version_ == Network::Address::IpVersion::v4) {
    simpleRequestAndResponse(request, "-MOVED 1111 127.0.0.1:34123\r\n");
    simpleRequestAndResponse(request, "-ASK 1111 127.0.0.1:34123\r\n");
  } else {
    simpleRequestAndResponse(request, "-MOVED 1111 ::1:34123\r\n");
    simpleRequestAndResponse(request, "-ASK 1111 ::1:34123\r\n");
  }
}

// This test sends an AUTH command from the fake downstream client to
// the Envoy proxy. Envoy will respond with a no-password-set error since
// no downstream_auth_password has been set for the filter.

TEST_P(RedisProxyIntegrationTest, DownstreamAuthWhenNoPasswordSet) {
  initialize();
  simpleProxyResponse(makeBulkStringArray({"auth", "somepassword"}),
                      "-ERR Client sent AUTH, but no password is set\r\n");
}

// This test sends a simple Redis command to a sequence of fake upstream
// Redis servers. The first server replies with a MOVED or ASK redirection
// error that specifies the second upstream server in the static configuration
// as its target. The target server responds to a possibly transformed
// request, and its response is received unchanged by the fake Redis client.

TEST_P(RedisProxyWithRedirectionIntegrationTest, RedirectToKnownServer) {
  std::string request = makeBulkStringArray({"get", "foo"});
  initialize();
  std::stringstream redirection_error;
  redirection_error << "-MOVED 1111 " << redisAddressAndPortNoThrow(fake_upstreams_[1]) << "\r\n";
  simpleRedirection(fake_upstreams_[1], request, redirection_error.str(), "$3\r\nbar\r\n");

  redirection_error.str("");
  redirection_error << "-ASK 1111 " << redisAddressAndPortNoThrow(fake_upstreams_[1]) << "\r\n";
  simpleRedirection(fake_upstreams_[1], request, redirection_error.str(), "$3\r\nbar\r\n");
}

// This test sends a simple Redis command to a sequence of fake upstream
// Redis servers. The first server replies with a MOVED redirection
// error that specifies the hostname as its target.
// The target server responds to a possibly transformed request, and its response
// is received unchanged by the fake Redis client.
TEST_P(RedisProxyWithRedirectionAndDNSIntegrationTest, RedirectUsingHostname) {
  std::string request = makeBulkStringArray({"get", "foo"});
  initialize();
  std::stringstream redirection_error;
  redirection_error << "-MOVED 1111 " << redisHostnameAndPort(fake_upstreams_[1]) << "\r\n";
  simpleRedirection(fake_upstreams_[1], request, redirection_error.str(), "$3\r\nbar\r\n");
}

// This test sends a simple Redis commands to a sequence of fake upstream
// Redis servers. The first server replies with a MOVED or ASK redirection
// error that specifies an unknown upstream server not in its static configuration
// as its target. The target server responds to a possibly transformed
// request, and its response is received unchanged by the fake Redis client.

TEST_P(RedisProxyWithRedirectionIntegrationTest, RedirectToUnknownServer) {
  std::string request = makeBulkStringArray({"get", "foo"});
  initialize();

  FakeUpstreamPtr target_server{std::make_unique<FakeUpstream>(0, version_, upstreamConfig())};

  std::stringstream redirection_error;
  redirection_error << "-MOVED 1111 " << redisAddressAndPortNoThrow(target_server) << "\r\n";
  simpleRedirection(target_server, request, redirection_error.str(), "$3\r\nbar\r\n");

  redirection_error.str("");
  redirection_error << "-ASK 1111 " << redisAddressAndPortNoThrow(target_server) << "\r\n";
  simpleRedirection(target_server, request, redirection_error.str(), "$3\r\nbar\r\n");
}

// This test verifies that various forms of bad MOVED/ASK redirection errors
// from a fake Redis server are not acted upon, and are passed unchanged
// to the fake Redis client.

TEST_P(RedisProxyWithRedirectionIntegrationTest, BadRedirectStrings) {
  initialize();
  std::string request = makeBulkStringArray({"get", "foo"});

  // Test with truncated moved errors.
  simpleRequestAndResponse(request, "-MOVED 1111\r\n");
  simpleRequestAndResponse(request, "-MOVED\r\n");
  // Test with truncated ask errors.
  simpleRequestAndResponse(request, "-ASK 1111\r\n");
  simpleRequestAndResponse(request, "-ASK\r\n");
  // Test with a badly specified IP address and TCP port field.
  simpleRequestAndResponse(request, "-MOVED 2222 badfield\r\n");
  simpleRequestAndResponse(request, "-ASK 2222 badfield\r\n");
  // Test with a bad IP address specification.
  if (version_ == Network::Address::IpVersion::v4) {
    simpleRequestAndResponse(request, "-MOVED 2222 127.0:3333\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 127.0:3333\r\n");
  } else {
    simpleRequestAndResponse(request, "-MOVED 2222 ::11111:3333\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 ::11111:3333\r\n");
  }
  // Test with a bad IP address specification (not numeric).
  if (version_ == Network::Address::IpVersion::v4) {
    simpleRequestAndResponse(request, "-MOVED 2222 badaddress:3333\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 badaddress:3333\r\n");
  } else {
    simpleRequestAndResponse(request, "-MOVED 2222 badaddress:3333\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 badaddress:3333\r\n");
  }
  // Test with a bad TCP port specification (out of range).
  if (version_ == Network::Address::IpVersion::v4) {
    simpleRequestAndResponse(request, "-MOVED 2222 127.0.0.1:100000\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 127.0.0.1:100000\r\n");
  } else {
    simpleRequestAndResponse(request, "-MOVED 2222 ::1:1000000\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 ::1:1000000\r\n");
  }
  // Test with a bad TCP port specification (not numeric).
  if (version_ == Network::Address::IpVersion::v4) {
    simpleRequestAndResponse(request, "-MOVED 2222 127.0.0.1:badport\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 127.0.0.1:badport\r\n");
  } else {
    simpleRequestAndResponse(request, "-MOVED 2222 ::1:badport\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 ::1:badport\r\n");
  }
}

// This test verifies that an upstream connection failure during ask redirection processing is
// handled correctly. In this case the "asking" command and original client request have been sent
// to the target server, and then the connection is closed. The fake Redis client should receive an
// upstream failure error in response to its request.

TEST_P(RedisProxyWithRedirectionIntegrationTest, ConnectionFailureBeforeAskingResponse) {
  initialize();

  std::string request = makeBulkStringArray({"get", "foo"});
  std::stringstream redirection_error;
  redirection_error << "-ASK 1111 " << redisAddressAndPortNoThrow(fake_upstreams_[1]) << "\r\n";

  std::string proxy_to_server;
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_TRUE(redis_client->write(request));

  FakeRawConnectionPtr fake_upstream_connection_1, fake_upstream_connection_2;

  // Data from the client should always be routed to fake_upstreams_[0] by the load balancer.
  EXPECT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_1));
  EXPECT_TRUE(fake_upstream_connection_1->waitForData(request.size(), &proxy_to_server));
  // The data in request should be received by the first server, fake_upstreams_[0].
  EXPECT_EQ(request, proxy_to_server);
  proxy_to_server.clear();

  // Send the redirection_response from the first fake Redis server back to the proxy.
  EXPECT_TRUE(fake_upstream_connection_1->write(redirection_error.str()));
  // The proxy should initiate a new connection to the fake redis server, target_server, in
  // response.
  EXPECT_TRUE(fake_upstreams_[1]->waitForRawConnection(fake_upstream_connection_2));

  // The server, fake_upstreams_[1], should receive an "asking" command before the original request.
  std::string asking_request = makeBulkStringArray({"asking"});
  EXPECT_TRUE(fake_upstream_connection_2->waitForData(asking_request.size() + request.size(),
                                                      &proxy_to_server));
  EXPECT_EQ(asking_request + request, proxy_to_server);
  // Close the upstream connection before responding to the "asking" command.
  EXPECT_TRUE(fake_upstream_connection_2->close());

  // The fake Redis client should receive an upstream failure error from the proxy.
  std::stringstream error_response;
  error_response << "-" << RedisCmdSplitter::Response::get().UpstreamFailure << "\r\n";
  redis_client->waitForData(error_response.str());
  EXPECT_EQ(error_response.str(), redis_client->data());

  EXPECT_TRUE(fake_upstream_connection_1->close());
  redis_client->close();
}

// This test verifies that a ASK redirection error as a response to an "asking" command is ignored.
// This is a negative test scenario that should never happen since a Redis server will reply to an
// "asking" command with either a "cluster support not enabled" error or "OK".

TEST_P(RedisProxyWithRedirectionIntegrationTest, IgnoreRedirectionForAsking) {
  initialize();
  std::string request = makeBulkStringArray({"get", "foo"});
  std::stringstream redirection_error, asking_response;
  redirection_error << "-ASK 1111 " << redisAddressAndPortNoThrow(fake_upstreams_[1]) << "\r\n";
  asking_response << "-ASK 1111 " << redisAddressAndPortNoThrow(fake_upstreams_[0]) << "\r\n";
  simpleRedirection(fake_upstreams_[1], request, redirection_error.str(), "$3\r\nbar\r\n",
                    asking_response.str());
}

// This test verifies that batching works properly. If batching is enabled, when multiple
// clients make a request to a Redis server within a certain time window, they will be batched
// together. The below example, two clients send "GET foo", and Redis receives those two as
// a single concatenated request.

TEST_P(RedisProxyWithBatchingIntegrationTest, SimpleBatching) {
  initialize();

  const std::string& request = makeBulkStringArray({"get", "foo"});
  const std::string response = "$3\r\nbar\r\n";

  std::string proxy_to_server;
  IntegrationTcpClientPtr redis_client_1 = makeTcpConnection(lookupPort("redis_proxy"));
  IntegrationTcpClientPtr redis_client_2 = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_TRUE(redis_client_1->write(request));
  ASSERT_TRUE(redis_client_2->write(request));

  FakeRawConnectionPtr fake_upstream_connection;
  EXPECT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  EXPECT_TRUE(fake_upstream_connection->waitForData(request.size() * 2, &proxy_to_server));
  // The original request should be the same as the data received by the server.
  EXPECT_EQ(request + request, proxy_to_server);

  EXPECT_TRUE(fake_upstream_connection->write(response + response));
  redis_client_1->waitForData(response);
  redis_client_2->waitForData(response);
  // The original response should be received by the fake Redis client.
  EXPECT_EQ(response, redis_client_1->data());
  EXPECT_EQ(response, redis_client_2->data());

  redis_client_1->close();
  EXPECT_TRUE(fake_upstream_connection->close());
  redis_client_2->close();
  EXPECT_TRUE(fake_upstream_connection->close());
}

// This test verifies that it's possible to route keys to 3 different upstream pools.

TEST_P(RedisProxyWithRoutesIntegrationTest, SimpleRequestAndResponseRoutedByPrefix) {
  initialize();

  // roundtrip to cluster_0 (catch_all route)
  simpleRoundtripToUpstream(fake_upstreams_[0], makeBulkStringArray({"get", "toto"}),
                            "$3\r\nbar\r\n");

  // roundtrip to cluster_1 (prefix "foo:" route)
  simpleRoundtripToUpstream(fake_upstreams_[2], makeBulkStringArray({"get", "foo:123"}),
                            "$3\r\nbar\r\n");

  // roundtrip to cluster_2 (prefix "baz:" route)
  simpleRoundtripToUpstream(fake_upstreams_[4], makeBulkStringArray({"get", "baz:123"}),
                            "$3\r\nbar\r\n");
}

// This test verifies that a client connection cannot issue a command to an upstream
// server until it supplies a valid Redis AUTH command when downstream_auth_password
// is set for the redis_proxy filter. It also verifies the errors sent by the proxy
// when no password or the wrong password is received.

TEST_P(RedisProxyWithDownstreamAuthIntegrationTest,
       DEPRECATED_FEATURE_TEST(ErrorsUntilCorrectPasswordSent)) {
  initialize();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;

  proxyResponseStep(makeBulkStringArray({"get", "foo"}), "-NOAUTH Authentication required.\r\n",
                    redis_client);

  std::stringstream error_response;
  error_response << "-" << RedisCmdSplitter::Response::get().InvalidRequest << "\r\n";
  proxyResponseStep(makeBulkStringArray({"auth"}), error_response.str(), redis_client);

  proxyResponseStep(makeBulkStringArray({"auth", "wrongpassword"}), "-ERR invalid password\r\n",
                    redis_client);

  proxyResponseStep(makeBulkStringArray({"get", "foo"}), "-NOAUTH Authentication required.\r\n",
                    redis_client);

  proxyResponseStep(makeBulkStringArray({"auth", "somepassword"}), "+OK\r\n", redis_client);

  roundtripToUpstreamStep(fake_upstreams_[0], makeBulkStringArray({"get", "foo"}), "$3\r\nbar\r\n",
                          redis_client, fake_upstream_connection, "", "");

  EXPECT_TRUE(fake_upstream_connection->close());
  redis_client->close();
}

TEST_P(RedisProxyWithMultipleDownstreamAuthIntegrationTest, ErrorsUntilCorrectPasswordSent1) {
  initialize();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;

  proxyResponseStep(makeBulkStringArray({"get", "foo"}), "-NOAUTH Authentication required.\r\n",
                    redis_client);

  std::stringstream error_response;
  error_response << "-" << RedisCmdSplitter::Response::get().InvalidRequest << "\r\n";
  proxyResponseStep(makeBulkStringArray({"auth"}), error_response.str(), redis_client);

  proxyResponseStep(makeBulkStringArray({"auth", "wrongpassword"}), "-ERR invalid password\r\n",
                    redis_client);

  proxyResponseStep(makeBulkStringArray({"get", "foo"}), "-NOAUTH Authentication required.\r\n",
                    redis_client);

  proxyResponseStep(makeBulkStringArray({"auth", "somepassword"}), "+OK\r\n", redis_client);

  roundtripToUpstreamStep(fake_upstreams_[0], makeBulkStringArray({"get", "foo"}), "$3\r\nbar\r\n",
                          redis_client, fake_upstream_connection, "", "");

  EXPECT_TRUE(fake_upstream_connection->close());
  redis_client->close();
}

TEST_P(RedisProxyWithMultipleDownstreamAuthIntegrationTest, ErrorsUntilCorrectPasswordSent2) {
  initialize();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;

  proxyResponseStep(makeBulkStringArray({"get", "foo"}), "-NOAUTH Authentication required.\r\n",
                    redis_client);

  std::stringstream error_response;
  error_response << "-" << RedisCmdSplitter::Response::get().InvalidRequest << "\r\n";
  proxyResponseStep(makeBulkStringArray({"auth"}), error_response.str(), redis_client);

  proxyResponseStep(makeBulkStringArray({"auth", "wrongpassword"}), "-ERR invalid password\r\n",
                    redis_client);

  proxyResponseStep(makeBulkStringArray({"get", "foo"}), "-NOAUTH Authentication required.\r\n",
                    redis_client);

  proxyResponseStep(makeBulkStringArray({"auth", "someotherpassword"}), "+OK\r\n", redis_client);

  roundtripToUpstreamStep(fake_upstreams_[0], makeBulkStringArray({"get", "foo"}), "$3\r\nbar\r\n",
                          redis_client, fake_upstream_connection, "", "");

  EXPECT_TRUE(fake_upstream_connection->close());
  redis_client->close();
}

// This test verifies that upstream server connections are transparently authenticated if an
// auth_password is specified for each cluster.

TEST_P(RedisProxyWithRoutesAndAuthPasswordsIntegrationTest, TransparentAuthentication) {

  initialize();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  std::array<FakeRawConnectionPtr, 3> fake_upstream_connection;

  // roundtrip to cluster_0 (catch_all route)
  roundtripToUpstreamStep(fake_upstreams_[0], makeBulkStringArray({"get", "toto"}), "$3\r\nbar\r\n",
                          redis_client, fake_upstream_connection[0], "", "cluster_0_password");

  // roundtrip to cluster_1 (prefix "foo:" route)
  roundtripToUpstreamStep(fake_upstreams_[1], makeBulkStringArray({"get", "foo:123"}),
                          "$3\r\nbar\r\n", redis_client, fake_upstream_connection[1], "",
                          "cluster_1_password");

  // roundtrip to cluster_2 (prefix "baz:" route)
  roundtripToUpstreamStep(fake_upstreams_[2], makeBulkStringArray({"get", "baz:123"}),
                          "$3\r\nbar\r\n", redis_client, fake_upstream_connection[2], "",
                          "cluster_2_password");

  EXPECT_TRUE(fake_upstream_connection[0]->close());
  EXPECT_TRUE(fake_upstream_connection[1]->close());
  EXPECT_TRUE(fake_upstream_connection[2]->close());
  redis_client->close();
}

TEST_P(RedisProxyWithRoutesAndAuthPasswordsAwsIamIntegrationTest, TransparentAuthentication) {

  TestEnvironment::setEnvVar("AWS_ACCESS_KEY_ID", "akid", 1);
  TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "secret", 1);
  TestEnvironment::setEnvVar("AWS_SESSION_TOKEN", "token", 1);
  simTime().setSystemTime(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::milliseconds(1514862245000)));
  initialize();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  std::array<FakeRawConnectionPtr, 3> fake_upstream_connection;

  // roundtrip to cluster_0 (catch_all route). The IAM init pipeline holds the user request
  // until the upstream AUTH +OK arrives, so the helper must stage the AUTH ack — see
  // expectUpstreamRequestResponse's stage_auth_ack branch for the rationale.
  roundtripToUpstreamStep(
      fake_upstreams_[0], makeBulkStringArray({"get", "toto"}), "$3\r\nbar\r\n", redis_client,
      fake_upstream_connection[0], "cluster_0_username",
      "testcache/"
      "?Action=connect&User=cluster_0_username&X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential="
      "akid%2F20180102%2Fus-east-1%2Felasticache%2Faws4_request&X-Amz-Date=20180102T030405Z&X-Amz-"
      "Expires=900&X-Amz-Security-Token=token&X-Amz-Signature="
      "b31882a92ff7ef159e6d19bf422a1019d28e88fbfc04c4c94a215134f0b69c2e&X-Amz-SignedHeaders=host",
      /*stage_auth_ack=*/true);

  // roundtrip to cluster_1 (prefix "foo:" route)
  roundtripToUpstreamStep(
      fake_upstreams_[1], makeBulkStringArray({"get", "foo:123"}), "$3\r\nbar\r\n", redis_client,
      fake_upstream_connection[1], "cluster_1_username",
      "testcache/"
      "?Action=connect&User=cluster_1_username&X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential="
      "akid%2F20180102%2Fus-east-1%2Felasticache%2Faws4_request&X-Amz-Date=20180102T030405Z&X-Amz-"
      "Expires=900&X-Amz-Security-Token=token&X-Amz-Signature="
      "8dd2faa4d1ba56ae8e45c24b7cd20d4d7b41acf15e48c199fad7484c4bacf8ef&X-Amz-SignedHeaders=host",
      /*stage_auth_ack=*/true);

  // roundtrip to cluster_2 (prefix "baz:" route)
  roundtripToUpstreamStep(
      fake_upstreams_[2], makeBulkStringArray({"get", "baz:123"}), "$3\r\nbar\r\n", redis_client,
      fake_upstream_connection[2], "cluster_2_username",
      "testcache/"
      "?Action=connect&User=cluster_2_username&X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential="
      "akid%2F20180102%2Fus-east-1%2Felasticache%2Faws4_request&X-Amz-Date=20180102T030405Z&X-Amz-"
      "Expires=900&X-Amz-Security-Token=token&X-Amz-Signature="
      "0b2d4d6304834c7104fc39c29b7a9e93dbdc400fb72a422b3f0a72ef2366c5f8&X-Amz-SignedHeaders=host",
      /*stage_auth_ack=*/true);

  EXPECT_TRUE(fake_upstream_connection[0]->close());
  EXPECT_TRUE(fake_upstream_connection[1]->close());
  EXPECT_TRUE(fake_upstream_connection[2]->close());
  redis_client->close();
}

// Test that we don't attempt IAM Auth if no auth username is set, even if iam auth configuration is
// set
TEST_P(RedisProxyWithRoutesAndAuthPasswordsAwsIamNoAuthUsernameIntegrationTest,
       TransparentAuthentication) {

  initialize();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;

  // roundtrip to cluster_0 (catch_all route)
  roundtripToUpstreamStep(fake_upstreams_[0], makeBulkStringArray({"get", "toto"}), "$3\r\nbar\r\n",
                          redis_client, fake_upstream_connection, "", "");

  EXPECT_TRUE(fake_upstream_connection->close());
  redis_client->close();
}

// This test verifies that correct credentials are used for each upstream server.
TEST_P(RedisProxyWithSeparateAuthPasswordsIntegrationTest, TransparentAuthentication) {
  const std::vector<std::string> endpoint_usernames = {"endpoint_0_username", "endpoint_1_username",
                                                       "endpoint_2_username"};
  const std::vector<std::string> endpoint_passwords = {"endpoint_0_password", "endpoint_1_password",
                                                       "endpoint_2_password"};

  config_helper_.addConfigModifier([this, &endpoint_usernames, &endpoint_passwords](
                                       envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    Envoy::Protobuf::Any& redis_proxy_config_any = bootstrap.mutable_static_resources()
                                                       ->mutable_clusters(0)
                                                       ->mutable_typed_extension_protocol_options()
                                                       ->at("envoy.filters.network.redis_proxy");
    envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions redis_proxy_config;
    ASSERT_TRUE(redis_proxy_config_any.UnpackTo(&redis_proxy_config));

    // The credentials need to be set here instead of the config string at the top because the
    // address changes depending on whether the test is run for IPv4 or IPv6, and the port changes
    // on each run. And then it seems like the username and password being set here is simpler and
    // less error-prone then specifying the strings in multiple places.
    for (int i = 0; i < 3; ++i) {
      auto* credential = redis_proxy_config.add_credentials();
      auto* socket_address = credential->mutable_address()->mutable_socket_address();
      socket_address->set_address(fake_upstreams_[i]->localAddress()->ip()->addressAsString());
      socket_address->set_port_value(fake_upstreams_[i]->localAddress()->ip()->port());
      credential->mutable_auth_username()->set_inline_string(endpoint_usernames[i]);
      credential->mutable_auth_password()->set_inline_string(endpoint_passwords[i]);
    }

    ASSERT_TRUE(redis_proxy_config_any.PackFrom(
        redis_proxy_config,
        "type.googleapis.com/"
        "envoy.extensions.filters.network.redis_proxy.v3.RedisProtocolOptions"));
  });

  initialize();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  std::vector<FakeRawConnectionPtr> fake_upstream_connections;
  fake_upstream_connections.resize(3);
  absl::flat_hash_set<uint64_t> indices = {0, 1, 2};
  const std::string response = "$3\r\nbar\r\n";

  // The key to upstream assignment changes on each test run because the ports are not fixed. So we
  // iterate a lot to hopefully hit all upstreams.
  for (int i = 0; i < 100; ++i) {
    const std::string request = makeBulkStringArray({"get", Envoy::Random::RandomUtility::uuid()});
    std::optional<uint64_t> upstream_index;
    roundtripToSomeUpstreamStep(fake_upstreams_, request, response, redis_client,
                                fake_upstream_connections, endpoint_usernames, endpoint_passwords,
                                upstream_index);
    ASSERT(upstream_index.has_value());
    indices.erase(upstream_index.value());
    if (indices.empty()) {
      break;
    }
  }

  ASSERT(indices.empty());
  redis_client->close();
  for (auto& fake_upstream_connection : fake_upstream_connections) {
    if (fake_upstream_connection != nullptr) {
      EXPECT_TRUE(fake_upstream_connection->close());
    }
  }
}

TEST_P(RedisProxyWithMirrorsIntegrationTest, MirroredCatchAllRequest) {
  initialize();

  std::array<FakeRawConnectionPtr, 3> fake_upstream_connection;
  const std::string& request = makeBulkStringArray({"get", "toto"});
  const std::string response = "$3\r\nbar\r\n";
  // roundtrip to cluster_0 (catch_all route)
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_TRUE(redis_client->write(request));

  expectUpstreamRequestResponse(fake_upstreams_[0], request, response, fake_upstream_connection[0]);

  // mirror to cluster_1 and cluster_2
  expectUpstreamRequestResponse(fake_upstreams_[2], request, "$4\r\nbar1\r\n",
                                fake_upstream_connection[1]);
  expectUpstreamRequestResponse(fake_upstreams_[4], request, "$4\r\nbar2\r\n",
                                fake_upstream_connection[2]);

  redis_client->waitForData(response);
  // The original response from the cluster_0 should be received by the fake Redis client and the
  // response from mirrored requests are ignored.
  EXPECT_EQ(response, redis_client->data());

  EXPECT_TRUE(fake_upstream_connection[0]->close());
  EXPECT_TRUE(fake_upstream_connection[1]->close());
  EXPECT_TRUE(fake_upstream_connection[2]->close());
  redis_client->close();
}

TEST_P(RedisProxyWithMirrorsIntegrationTest, MirroredTransaction) {
  initialize();

  std::array<FakeRawConnectionPtr, 3> fake_upstream_connection;

  std::string request = makeBulkStringArray({"MULTI"}) +
                        makeBulkStringArray({"set", "foo", "bar"}) +
                        makeBulkStringArray({"get", "foo"}) + makeBulkStringArray({"exec"});
  const std::string response = "+OK\r\n+QUEUED\r\n+QUEUED\r\n*2\r\n+OK\r\n$3\r\nbar\r\n";

  // roundtrip to cluster_0 (catch_all route)
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_TRUE(redis_client->write(request));

  expectUpstreamRequestResponse(fake_upstreams_[0], request, response, fake_upstream_connection[0]);

  // mirror to cluster_1 and cluster_2
  expectUpstreamRequestResponse(fake_upstreams_[2], request, "$4\r\nbar1\r\n",
                                fake_upstream_connection[1]);
  expectUpstreamRequestResponse(fake_upstreams_[4], request, "$4\r\nbar2\r\n",
                                fake_upstream_connection[2]);

  redis_client->waitForData(response);
  // The original response from the cluster_0 should be received by the fake Redis client and the
  // response from mirrored requests are ignored.
  EXPECT_EQ(response, redis_client->data());

  EXPECT_TRUE(fake_upstream_connection[0]->close());
  EXPECT_TRUE(fake_upstream_connection[1]->close());
  EXPECT_TRUE(fake_upstream_connection[2]->close());
  redis_client->close();
}

TEST_P(RedisProxyWithMirrorsIntegrationTest, MirroredWriteOnlyRequest) {
  initialize();

  std::array<FakeRawConnectionPtr, 2> fake_upstream_connection;
  const std::string& set_request = makeBulkStringArray({"set", "write_only:toto", "bar"});
  const std::string set_response = ":1\r\n";

  // roundtrip to cluster_0 (write_only route)
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_TRUE(redis_client->write(set_request));

  expectUpstreamRequestResponse(fake_upstreams_[0], set_request, set_response,
                                fake_upstream_connection[0]);

  // mirror to cluster_1
  expectUpstreamRequestResponse(fake_upstreams_[2], set_request, ":2\r\n",
                                fake_upstream_connection[1]);

  // The original response from the cluster_1 should be received by the fake Redis client
  redis_client->waitForData(set_response);
  EXPECT_EQ(set_response, redis_client->data());

  EXPECT_TRUE(fake_upstream_connection[0]->close());
  EXPECT_TRUE(fake_upstream_connection[1]->close());
  redis_client->close();
}

TEST_P(RedisProxyWithMirrorsIntegrationTest, ExcludeReadCommands) {
  initialize();

  FakeRawConnectionPtr cluster_0_connection;
  const std::string& get_request = makeBulkStringArray({"get", "write_only:toto"});
  const std::string get_response = "$3\r\nbar\r\n";

  // roundtrip to cluster_0 (write_only route)
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_TRUE(redis_client->write(get_request));

  expectUpstreamRequestResponse(fake_upstreams_[0], get_request, get_response,
                                cluster_0_connection);

  // command is not mirrored to cluster 1
  FakeRawConnectionPtr cluster_1_connection;
  EXPECT_FALSE(fake_upstreams_[2]->waitForRawConnection(cluster_1_connection,
                                                        std::chrono::milliseconds(500)));

  redis_client->waitForData(get_response);
  EXPECT_EQ(get_response, redis_client->data());

  EXPECT_TRUE(cluster_0_connection->close());
  redis_client->close();
}

TEST_P(RedisProxyWithMirrorsIntegrationTest, EnabledViaRuntimeFraction) {
  initialize();

  std::array<FakeRawConnectionPtr, 2> fake_upstream_connection;
  // When random_value is < 50, the percentage:* will be mirrored, random() default is 0
  const std::string& request = makeBulkStringArray({"get", "percentage:toto"});
  const std::string response = "$3\r\nbar\r\n";
  // roundtrip to cluster_0 (catch_all route)
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_TRUE(redis_client->write(request));

  expectUpstreamRequestResponse(fake_upstreams_[0], request, response, fake_upstream_connection[0]);

  // mirror to cluster_1
  expectUpstreamRequestResponse(fake_upstreams_[2], request, "$4\r\nbar1\r\n",
                                fake_upstream_connection[1]);

  redis_client->waitForData(response);
  // The original response from the cluster_0 should be received by the fake Redis client and the
  // response from mirrored requests are ignored.
  EXPECT_EQ(response, redis_client->data());

  EXPECT_TRUE(fake_upstream_connection[0]->close());
  EXPECT_TRUE(fake_upstream_connection[1]->close());
  redis_client->close();
}

TEST_P(RedisProxyWithFaultInjectionIntegrationTest, ErrorFault) {
  std::string fault_response =
      fmt::format("-{}\r\n", Extensions::NetworkFilters::Common::Redis::FaultMessages::get().Error);
  initialize();
  simpleProxyResponse(makeBulkStringArray({"get", "foo"}), fault_response);

  EXPECT_EQ(1, test_server_->counter("redis.redis_stats.command.get.error")->value());
  EXPECT_EQ(1, test_server_->counter("redis.redis_stats.command.get.error_fault")->value());
}

TEST_P(RedisProxyWithFaultInjectionIntegrationTest, DelayFault) {
  const std::string& set_request = makeBulkStringArray({"set", "write_only:toto", "bar"});
  const std::string set_response = ":1\r\n";
  initialize();
  simpleRequestAndResponse(set_request, set_response);

  EXPECT_EQ(1, test_server_->counter("redis.redis_stats.command.set.success")->value());
  EXPECT_EQ(1, test_server_->counter("redis.redis_stats.command.set.delay_fault")->value());
}

// This test sends a MULTI Redis command from a fake
// downstream client to the envoy proxy. Envoy will respond
// with an OK.

TEST_P(RedisProxyIntegrationTest, SendMulti) {
  initialize();
  simpleProxyResponse(makeBulkStringArray({"multi"}), "+OK\r\n");
}

// This test sends a nested MULTI Redis command from a fake
// downstream client to the envoy proxy. Envoy will respond with an
// error.

TEST_P(RedisProxyIntegrationTest, SendNestedMulti) {
  initialize();
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));

  proxyResponseStep(makeBulkStringArray({"multi"}), "+OK\r\n", redis_client);
  proxyResponseStep(makeBulkStringArray({"multi"}), "-MULTI calls can not be nested\r\n",
                    redis_client);

  redis_client->close();
}

// This test sends an EXEC command without a MULTI command
// preceding it. The proxy responds with an error.

TEST_P(RedisProxyIntegrationTest, ExecWithoutMulti) {
  initialize();
  simpleProxyResponse(makeBulkStringArray({"exec"}), "-EXEC without MULTI\r\n");
}

// This test sends an DISCARD command without a MULTI command
// preceding it. The proxy responds with an error.

TEST_P(RedisProxyIntegrationTest, DiscardWithoutMulti) {
  initialize();
  simpleProxyResponse(makeBulkStringArray({"discard"}), "-DISCARD without MULTI\r\n");
}

// This test executes an empty transaction. The proxy responds
// with an empty array.

TEST_P(RedisProxyIntegrationTest, ExecuteEmptyTransaction) {
  initialize();
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));

  proxyResponseStep(makeBulkStringArray({"multi"}), "+OK\r\n", redis_client);
  proxyResponseStep(makeBulkStringArray({"exec"}), "*0\r\n", redis_client);

  redis_client->close();
}

TEST_P(RedisProxyIntegrationTest, UnwatchNoTransactionNoOp) {
  initialize();
  simpleProxyResponse(makeBulkStringArray({"unwatch"}), "+OK\r\n");
}

TEST_P(RedisProxyIntegrationTest, UnwatchWithTransactionNoOp) {
  initialize();
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));

  proxyResponseStep(makeBulkStringArray({"multi"}), "+OK\r\n", redis_client);
  proxyResponseStep(makeBulkStringArray({"unwatch"}), "+QUEUED\r\n", redis_client);

  redis_client->close();
}

TEST_P(RedisProxyIntegrationTest, WatchUnwatchNoTransaction) {
  initialize();
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));

  FakeUpstreamPtr& upstream = fake_upstreams_[0];
  FakeRawConnectionPtr fake_upstream_conn;

  roundtripToUpstreamStep(upstream, makeBulkStringArray({"watch", "foo"}), "+OK\r\n", redis_client,
                          fake_upstream_conn, "", "");

  roundtripToUpstreamStep(upstream, makeBulkStringArray({"unwatch"}), "+OK\r\n", redis_client,
                          fake_upstream_conn, "", "");

  EXPECT_TRUE(fake_upstream_conn->close());
  redis_client->close();
}

TEST_P(RedisProxyIntegrationTest, WatchUnwatchUnrelatedTransaction) {
  initialize();
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));

  FakeUpstreamPtr& upstream = fake_upstreams_[0];
  FakeRawConnectionPtr fake_upstream_conn;

  roundtripToUpstreamStep(upstream, makeBulkStringArray({"watch", "foo"}), "+OK\r\n", redis_client,
                          fake_upstream_conn, "", "");

  roundtripToUpstreamStep(upstream, makeBulkStringArray({"unwatch"}), "+OK\r\n", redis_client,
                          fake_upstream_conn, "", "");

  proxyResponseStep(makeBulkStringArray({"multi"}), "+OK\r\n", redis_client);
  proxyResponseStep(makeBulkStringArray({"discard"}), "+OK\r\n", redis_client);

  EXPECT_TRUE(fake_upstream_conn->close());
  redis_client->close();
}

TEST_P(RedisProxyIntegrationTest, WatchUnwatchInTransaction) {
  initialize();
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));

  FakeUpstreamPtr& upstream = fake_upstreams_[0];
  FakeRawConnectionPtr fake_upstream_conn;

  roundtripToUpstreamStep(upstream, makeBulkStringArray({"watch", "foo"}), "+OK\r\n", redis_client,
                          fake_upstream_conn, "", "");

  // MULTI will create a new connection, for the transaction.
  FakeRawConnectionPtr fake_upstream_conn2;
  roundtripToUpstreamStep(upstream, makeBulkStringArray({"multi"}), "+OK\r\n", redis_client,
                          fake_upstream_conn2, "", "");

  roundtripToUpstreamStep(upstream, makeBulkStringArray({"unwatch"}), "+QUEUED\r\n", redis_client,
                          fake_upstream_conn2, "", "");

  EXPECT_TRUE(fake_upstream_conn->close());
  EXPECT_TRUE(fake_upstream_conn2->close());
  redis_client->close();
}
// This test discards an empty transaction. The proxy responds
// with an OK.

TEST_P(RedisProxyIntegrationTest, DiscardEmptyTransaction) {
  initialize();
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));

  proxyResponseStep(makeBulkStringArray({"multi"}), "+OK\r\n", redis_client);
  proxyResponseStep(makeBulkStringArray({"discard"}), "+OK\r\n", redis_client);

  redis_client->close();
}

// MultiKey commands are allowed to go through. The underlying server may fail with
// CROSSSLOT or MOVED errors, and the client must handle it.
TEST_P(RedisProxyIntegrationTest, MultiKeyCommandInTransaction) {
  initialize();
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));

  FakeUpstreamPtr& upstream = fake_upstreams_[0];
  FakeRawConnectionPtr fake_upstream_conn;

  proxyResponseStep(makeBulkStringArray({"multi"}), "+OK\r\n", redis_client);

  ASSERT_TRUE(redis_client->write(makeBulkStringArray({"del", "b"})));
  expectUpstreamRequestResponse(upstream,
                                makeBulkStringArray({"MULTI"}) + makeBulkStringArray({"del", "b"}),
                                "+QUEUED\r\n", fake_upstream_conn, "", "");

  roundtripToUpstreamStep(upstream, makeBulkStringArray({"del", "a"}),
                          "-ERR MOVED 15495 0.0.0.0:6379\r\n", redis_client, fake_upstream_conn, "",
                          "");

  roundtripToUpstreamStep(upstream, makeBulkStringArray({"del", "b", "a"}),
                          "-ERR CROSSSLOT Keys in request don't hash to the same slot\r\n",
                          redis_client, fake_upstream_conn, "", "");

  EXPECT_TRUE(fake_upstream_conn->close());
  redis_client->close();
}

// This test verifies that a multi command is sent before the first
// simple command of the transaction.

TEST_P(RedisProxyWithCommandStatsIntegrationTest, SendMultiBeforeCommandInTransaction) {
  initialize();
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));

  proxyResponseStep(makeBulkStringArray({"multi"}), "+OK\r\n", redis_client);

  std::string request = makeBulkStringArray({"set", "foo", "bar"});
  ASSERT_TRUE(redis_client->write(request));

  // The upstream will receive a MULTI command before the SET command.
  FakeUpstreamPtr& upstream = fake_upstreams_[0];
  FakeRawConnectionPtr fake_upstream_connection;
  std::string auth_username = "";
  std::string auth_password = "";
  std::string upstream_request =
      makeBulkStringArray({"MULTI"}) + makeBulkStringArray({"set", "foo", "bar"});
  std::string upstream_response = "";
  expectUpstreamRequestResponse(upstream, upstream_request, upstream_response,
                                fake_upstream_connection, auth_username, auth_password);

  EXPECT_TRUE(fake_upstream_connection->close());
  redis_client->close();
}

// This test verifies that a transaction can be pipelined.
TEST_P(RedisProxyWithCommandStatsIntegrationTest, PipelinedTransactionTest) {
  initialize();

  std::array<FakeRawConnectionPtr, 1> fake_upstream_connection;
  std::string transaction_commands =
      makeBulkStringArray({"MULTI"}) + makeBulkStringArray({"set", "foo", "bar"}) +
      makeBulkStringArray({"get", "foo"}) + makeBulkStringArray({"exec"});
  const std::string response = "+OK\r\n+QUEUED\r\n+QUEUED\r\n*2\r\n+OK\r\n$3\r\nbar\r\n";
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_TRUE(redis_client->write(transaction_commands));

  expectUpstreamRequestResponse(fake_upstreams_[0], transaction_commands, response,
                                fake_upstream_connection[0]);

  redis_client->waitForData(response);
  EXPECT_EQ(response, redis_client->data());

  EXPECT_TRUE(fake_upstream_connection[0]->close());
  redis_client->close();
}

// This test verifies that a configured custom command is treated as a simple
// command and can be used within a transaction.
TEST_P(RedisProxyWithCustomCommandsIntegrationTest, PipelinedTransactionWithCustomCommand) {
  initialize();

  std::array<FakeRawConnectionPtr, 1> fake_upstream_connection;
  std::string transaction_commands = makeBulkStringArray({"MULTI"}) +
                                     makeBulkStringArray({"json.set", "foo", "$", "1"}) +
                                     makeBulkStringArray({"exec"});
  const std::string& response = "+OK\r\n+QUEUED\r\n*1\r\n+OK\r\n";
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  ASSERT_TRUE(redis_client->write(transaction_commands));

  expectUpstreamRequestResponse(fake_upstreams_[0], transaction_commands, response,
                                fake_upstream_connection[0]);

  redis_client->waitForData(response);
  EXPECT_EQ(response, redis_client->data());

  EXPECT_TRUE(fake_upstream_connection[0]->close());
  redis_client->close();
}

// TODO: Add full transaction semantics coverage (DISCARD/WATCH/abort/error paths).

TEST_P(RedisProxyWithExternalAuthIntegrationTest, ErrorsUntilCorrectPasswordSentExternal) {
  initialize();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  FakeRawConnectionPtr fake_upstream_redis_connection;
  FakeHttpConnectionPtr fake_upstream_external_auth;

  // Trying to get a key without authenticating should return NOAUTH error.
  proxyResponseStep(makeBulkStringArray({"get", "foo"}), "-NOAUTH Authentication required.\r\n",
                    redis_client);

  // Sending an AUTH command without a password should return an error.
  std::stringstream error_response;
  error_response << "-" << RedisCmdSplitter::Response::get().InvalidRequest << "\r\n";
  proxyResponseStep(makeBulkStringArray({"auth"}), error_response.str(), redis_client);

  // Sending an AUTH command with the wrong password should return an error.
  proxyRequestStep(makeBulkStringArray({"auth", "wrongpassword"}), redis_client);
  FakeStreamPtr auth_request1;
  expectExternalAuthRequest(fake_upstream_external_auth, auth_request1, "wrongpassword");
  sendExternalAuthResponse(auth_request1, false, 0);
  proxyResponseOnlyStep("-ERR Unauthorized\r\n", redis_client);

  // Sending a command after unsuccessful auth should return NOAUTH error.
  proxyResponseStep(makeBulkStringArray({"get", "foo"}), "-NOAUTH Authentication required.\r\n",
                    redis_client);

  // Sending an AUTH command with the correct password should return OK.
  proxyRequestStep(makeBulkStringArray({"auth", "somepassword"}), redis_client);
  FakeStreamPtr auth_request2;
  expectExternalAuthRequest(fake_upstream_external_auth, auth_request2, "somepassword", true);
  auto expiration_time = timeSystem().systemTime() + std::chrono::hours(1);
  sendExternalAuthResponse(
      auth_request2, true,
      duration_cast<std::chrono::seconds>(expiration_time.time_since_epoch()).count());
  proxyResponseOnlyStep("+OK\r\n", redis_client);

  // Further requests should be successful.
  roundtripToUpstreamStep(fake_upstreams_[0], makeBulkStringArray({"get", "foo"}), "$3\r\nbar\r\n",
                          redis_client, fake_upstream_redis_connection, "", "");

  // Expiration passes
  timeSystem().advanceTimeWait(std::chrono::hours(2));

  // Sending a command after expiration should return NOAUTH error.
  proxyResponseStep(makeBulkStringArray({"get", "foo"}), "-NOAUTH Authentication required.\r\n",
                    redis_client);

  EXPECT_TRUE(fake_upstream_redis_connection->close());
  EXPECT_TRUE(fake_upstream_external_auth->close());
  redis_client->close();
}

TEST_P(RedisProxyWithExternalAuthIntegrationTest, ExternalAuthRespectsPipelining) {
  initialize();

  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  FakeHttpConnectionPtr fake_upstream_external_auth;

  // Send an AUTH command followed immediately by a PING command should return OK and PONG.
  proxyRequestStep(makeBulkStringArray({"AUTH", "somepassword"}), redis_client);
  FakeStreamPtr auth_request;
  expectExternalAuthRequest(fake_upstream_external_auth, auth_request, "somepassword");
  auto expiration_time = timeSystem().systemTime() + std::chrono::hours(1);
  // PING is sent before the response to the AUTH command is received.
  proxyRequestStep(makeBulkStringArray({"PING"}), redis_client);
  sendExternalAuthResponse(
      auth_request, true,
      duration_cast<std::chrono::seconds>(expiration_time.time_since_epoch()).count());
  proxyResponseOnlyStep("+OK\r\n+PONG\r\n", redis_client);

  EXPECT_TRUE(fake_upstream_external_auth->close());
  redis_client->close();
}

} // namespace
} // namespace Envoy
