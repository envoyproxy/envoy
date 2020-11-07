#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "common/http/codec_client.h"

#include "extensions/filters/listener/proxy_protocol/proxy_protocol.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/server.h"

#include "gtest/gtest.h"

namespace Envoy {
class ProxyProtoIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public HttpIntegrationTest {
public:
  ProxyProtoIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          ::envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol proxy_protocol;
          auto rule = proxy_protocol.add_rules();
          rule->set_tlv_type(0x02);
          rule->mutable_on_tlv_present()->set_key("PP2TypeAuthority");

          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          auto* ppv_filter = listener->add_listener_filters();
          ppv_filter->set_name("envoy.listener.proxy_protocol");
          ppv_filter->mutable_typed_config()->PackFrom(proxy_protocol);
        });
  }
};
} // namespace Envoy
