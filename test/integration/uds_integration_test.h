#pragma once

#include "common/http/codec_client.h"
#include "common/stats/stats_impl.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
class UdsIntegrationTest : public HttpIntegrationTest,
                           public testing::TestWithParam<Network::Address::IpVersion> {
public:
  UdsIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void createUpstreams() override {
    fake_upstreams_.emplace_back(new FakeUpstream(
        TestEnvironment::unixDomainSocketPath("udstest.1.sock"), FakeHttpConnection::Type::HTTP1));

    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
          auto* static_resources = bootstrap.mutable_static_resources();
          for (int i = 0; i < static_resources->clusters_size(); ++i) {
            auto* cluster = static_resources->mutable_clusters(i);
            for (int j = 0; j < cluster->hosts_size(); ++j) {
              cluster->mutable_hosts(j)->clear_socket_address();
              cluster->mutable_hosts(j)->mutable_pipe()->set_path(
                  TestEnvironment::unixDomainSocketPath("udstest.1.sock"));
            }
          }
        });
  }
};
} // namespace Envoy
