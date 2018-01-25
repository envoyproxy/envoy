#pragma once

#include "common/http/codec_client.h"
#include "common/stats/stats_impl.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/server.h"

#include "gtest/gtest.h"

namespace Envoy {
class ProxyProtoIntegrationTest : public HttpIntegrationTest,
                                  public testing::TestWithParam<Network::Address::IpVersion> {
public:
  ProxyProtoIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          auto* filter_chain = listener->mutable_filter_chains(0);
          filter_chain->mutable_use_proxy_proto()->set_value(true);
        });
  }
};
} // namespace Envoy
