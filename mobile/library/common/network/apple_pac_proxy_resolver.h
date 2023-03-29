#pragma once

#include <CFNetwork/CFNetwork.h>

#include <functional>

#include "absl/strings/string_view.h"
#include "library/common/network/proxy_settings.h"

namespace Envoy {
namespace Network {

class ApplePACProxyResolver {
public:
  void resolveProxies(absl::string_view target_url_string,
                      absl::string_view proxy_autoconfiguration_file_url_string,
                      std::function<void(std::vector<ProxySettings>)>);

private:
  CFURLRef createCFURL(absl::string_view);
};

} // namespace Network
} // namespace Envoy
