#pragma once

#include <tuple>

#include "common/http/codec_client.h"
#include "common/stats/stats_impl.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
class UdsIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>> {
public:
  UdsIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, std::get<0>(GetParam())),
        abstract_namespace_(std::get<1>(GetParam())) {}

  void createUpstreams() override {
    const std::string socket_name = abstract_namespace_
                                        ? "@/udstest.1.sock"
                                        : TestEnvironment::unixDomainSocketPath("udstest.1.sock");
    fake_upstreams_.emplace_back(new FakeUpstream(socket_name, FakeHttpConnection::Type::HTTP1));

    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
          auto* static_resources = bootstrap.mutable_static_resources();
          for (int i = 0; i < static_resources->clusters_size(); ++i) {
            auto* cluster = static_resources->mutable_clusters(i);
            for (int j = 0; j < cluster->hosts_size(); ++j) {
              cluster->mutable_hosts(j)->clear_socket_address();
              cluster->mutable_hosts(j)->mutable_pipe()->set_path(socket_name);
            }
          }
        });
  }

private:
  const bool abstract_namespace_;
};
} // namespace Envoy
