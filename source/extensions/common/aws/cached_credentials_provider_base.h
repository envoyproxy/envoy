#pragma once

// #include <list>
// #include <memory>
// #include <optional>
// #include <string>

// #include "envoy/api/api.h"
// #include "envoy/common/optref.h"
// #include "envoy/config/core/v3/base.pb.h"
// #include "envoy/event/timer.h"
// #include "envoy/extensions/common/aws/v3/credential_provider.pb.h"
// #include "envoy/http/message.h"
// #include "envoy/server/factory_context.h"

// #include "source/common/common/lock_guard.h"
// #include "source/common/common/logger.h"
// #include "source/common/common/thread.h"
// #include "source/common/config/datasource.h"
// #include "source/common/init/target_impl.h"
// #include "source/common/protobuf/message_validator_impl.h"
// #include "source/common/protobuf/utility.h"
#include "envoy/common/time.h"

#include "source/extensions/common/aws/aws_cluster_manager.h"
#include "source/extensions/common/aws/credentials_provider.h"

// #include "source/extensions/common/aws/metadata_fetcher.h"

// #include "absl/strings/string_view.h"

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
