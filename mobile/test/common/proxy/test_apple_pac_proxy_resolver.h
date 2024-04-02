#pragma once

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>

#include "library/common/network/apple_pac_proxy_resolver.h"

namespace Envoy {
namespace Network {

class TestApplePacProxyResolver : public Network::ApplePacProxyResolver {
public:
  TestApplePacProxyResolver(const std::string& host, const int port);
  virtual ~TestApplePacProxyResolver() = default;

protected:
  CFRunLoopSourceRef createPacUrlResolverSource(CFURLRef cf_proxy_autoconfiguration_file_url,
                                                CFURLRef cf_target_url,
                                                CFStreamClientContext* context) override;

  const std::string host_;
  const int port_;
  std::unique_ptr<CFRunLoopSourceContext> run_loop_context_;
};

} // namespace Network
} // namespace Envoy
