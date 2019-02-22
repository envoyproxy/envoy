#include "test/integration/redis_proxy_integration_test.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"

namespace RedisCommon = Envoy::Extensions::NetworkFilters::Common::Redis;
namespace RedisCmdSplitter = Envoy::Extensions::NetworkFilters::RedisProxy::CommandSplitter;

namespace Envoy {
namespace {

void makeBulkStringArray(RedisCommon::RespValue& value, std::vector<std::string>& command_strings) {
  value.type(RedisCommon::RespType::Array);
  for (auto& str : command_strings) {
    RedisCommon::RespValue bs;
    bs.type(RedisCommon::RespType::BulkString);
    bs.asString() = str;
    value.asArray().push_back(bs);
  }
}

std::string valueToString(const RedisCommon::RespValue& value) {
  RedisCommon::EncoderImpl encoder;
  Buffer::OwnedImpl temp_buffer;

  encoder.encode(value, temp_buffer);
  return temp_buffer.toString();
}

RedisCommon::RespValuePtr stringToValue(const std::string& data) {
  TestDecoderCallbacks callbacks;
  RedisCommon::DecoderImpl decoder(callbacks);
  Buffer::OwnedImpl temp_buffer(data);

  decoder.decode(temp_buffer);
  return std::move(callbacks.decoded());
}

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

void RedisProxyIntegrationTest::initialize() {
  config_helper_.renameListener("redis_proxy");
  BaseIntegrationTest::initialize();
}

TEST_P(RedisProxyIntegrationTest, SimpleRequestAndResponse) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("redis_proxy"));

  std::vector<std::string> get_command = {"get", "foo"};
  RedisCommon::RespValue request;
  makeBulkStringArray(request, get_command);
  std::string client_to_proxy = valueToString(request);
  std::string proxy_to_server;

  ASSERT_TRUE(client_to_proxy.size() > 0);
  ASSERT_TRUE(client_to_proxy.find("get") != std::string::npos);
  tcp_client->write(client_to_proxy);

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(client_to_proxy.size(), &proxy_to_server));

  RedisCommon::RespValuePtr received_request(stringToValue(proxy_to_server));
  ASSERT_TRUE(received_request.get() != nullptr);
  ASSERT_EQ(request, *received_request);

  RedisCommon::RespValue response;
  response.type(RedisCommon::RespType::BulkString);
  response.asString() = "bar";
  std::string server_to_proxy = valueToString(response);

  ASSERT_TRUE(fake_upstream_connection->write(server_to_proxy));
  tcp_client->waitForData(server_to_proxy);
  ASSERT_EQ(server_to_proxy, tcp_client->data());

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->close());
}

TEST_P(RedisProxyIntegrationTest, InvalidRequest) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("redis_proxy"));

  std::vector<std::string> invalid_command = {"foo"};
  RedisCommon::RespValue request;
  makeBulkStringArray(request, invalid_command);
  std::string client_to_proxy = valueToString(request);

  ASSERT_TRUE(client_to_proxy.size() > 0);
  ASSERT_TRUE(client_to_proxy.find("foo") != std::string::npos);
  tcp_client->write(client_to_proxy);

  RedisCommon::RespValue error_response;
  error_response.type(RedisCommon::RespType::Error);
  error_response.asString() = RedisCmdSplitter::Response::get().InvalidRequest;
  std::string proxy_to_client = valueToString(error_response);

  tcp_client->waitForData(proxy_to_client);
  ASSERT_EQ(proxy_to_client, tcp_client->data());

  tcp_client->close();
}

} // namespace
} // namespace Envoy
