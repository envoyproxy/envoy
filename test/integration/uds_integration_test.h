#pragma once

#include <tuple>

#include "common/common/fmt.h"
#include "common/http/codec_client.h"
#include "common/stats/stats_impl.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
class UdsUpstreamIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>> {
public:
  UdsUpstreamIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, std::get<0>(GetParam())),
        abstract_namespace_(std::get<1>(GetParam())) {}

  void createUpstreams() override {
    fake_upstreams_.emplace_back(
        new FakeUpstream(getSocketName(), FakeHttpConnection::Type::HTTP1));

    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
          auto* static_resources = bootstrap.mutable_static_resources();
          for (int i = 0; i < static_resources->clusters_size(); ++i) {
            auto* cluster = static_resources->mutable_clusters(i);
            for (int j = 0; j < cluster->hosts_size(); ++j) {
              cluster->mutable_hosts(j)->clear_socket_address();
              cluster->mutable_hosts(j)->mutable_pipe()->set_path(getSocketName());
            }
          }
        });
  }

  std::string getSocketName() {
    return abstract_namespace_ ? "@/my/udstest"
                               : TestEnvironment::unixDomainSocketPath("udstest.1.sock");
  }

protected:
  const bool abstract_namespace_;
};

class UdsListenerIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>> {
public:
  UdsListenerIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, std::get<0>(GetParam())),
        abstract_namespace_(std::get<1>(GetParam())) {}

  void initialize() override;

  std::string getSocketName(const std::string& path) {
    const std::string name = TestEnvironment::unixDomainSocketPath(path);
    if (!abstract_namespace_) {
      return name;
    }
    return "@" + name;
  }

  std::string getAdminSocketName() { return getSocketName("admin.sock"); }

  std::string getListenerSocketName() { return getSocketName("listener_0.sock"); }

protected:
  HttpIntegrationTest::ConnectionCreationFunction createConnectionFn();

  const bool abstract_namespace_;
};

} // namespace Envoy
