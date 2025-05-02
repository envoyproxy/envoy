#pragma once
#include "envoy/common/time.h"

#include "source/extensions/common/aws/credentials_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

constexpr std::chrono::hours REFRESH_INTERVAL{1};

class CachedCredentialsProviderBase : public CredentialsProvider,
                                      public Logger::Loggable<Logger::Id::aws> {
public:
  Credentials getCredentials() override {
    refreshIfNeeded();
    return cached_credentials_;
  }
  bool credentialsPending() override { return false; };

protected:
  SystemTime last_updated_;
  Credentials cached_credentials_;

  void refreshIfNeeded() {
    if (needsRefresh()) {
      refresh();
    }
  }

  virtual bool needsRefresh() PURE;
  virtual void refresh() PURE;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
