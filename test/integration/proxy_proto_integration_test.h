#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "common/http/codec_client.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/server.h"

#include "gtest/gtest.h"

namespace Envoy {
class ProxyProtoIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion,
                                               FakeHttpConnection::Type, Http::CodecClient::Type>>,
      public HttpIntegrationTest {
public:
  ProxyProtoIntegrationTest()
      : HttpIntegrationTest(std::get<2>(GetParam()), std::get<0>(GetParam())) {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          auto* filter_chain = listener->mutable_filter_chains(0);
          filter_chain->mutable_use_proxy_proto()->set_value(true);
        });
    setUpstreamProtocol(std::get<1>(GetParam()));
  }
};
} // namespace Envoy
