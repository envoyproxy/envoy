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

  // Whether the stats store derives tags with the explicit-tags logic rather than the legacy
  // tag-extraction rules. Both modes must produce identical stat names, tag-extracted names and
  // tags, so the two IP versions run the server in different modes.
  bool explicitTags() const { return version_ == Network::Address::IpVersion::v6; }
  void initialize() override;
};

class ProxyProtoTcpIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public BaseIntegrationTest {
public:
  ProxyProtoTcpIntegrationTest();

  // See ProxyProtoIntegrationTest::explicitTags().
  bool explicitTags() const { return version_ == Network::Address::IpVersion::v6; }
  void initialize() override;
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
