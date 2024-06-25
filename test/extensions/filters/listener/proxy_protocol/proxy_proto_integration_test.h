#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "source/common/http/codec_client.h"
#include "source/extensions/filters/listener/proxy_protocol/proxy_protocol.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/server.h"

#include "gtest/gtest.h"

namespace Envoy {
class ProxyProtoIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public HttpIntegrationTest {
public:
  ProxyProtoIntegrationTest();
};

class ProxyProtoTcpIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public BaseIntegrationTest {
public:
  ProxyProtoTcpIntegrationTest();
};

class ProxyProtoFilterChainMatchIntegrationTest : public ProxyProtoTcpIntegrationTest {
public:
  ProxyProtoFilterChainMatchIntegrationTest();

  void send(const std::string& data);
};

class ProxyProtoDisallowedVersionsIntegrationTest : public ProxyProtoTcpIntegrationTest {
public:
  ProxyProtoDisallowedVersionsIntegrationTest();
};

} // namespace Envoy
