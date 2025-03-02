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
#include "source/common/common/logger.h"

// #include "source/common/common/thread.h"
// #include "source/common/config/datasource.h"
// #include "source/common/init/target_impl.h"
// #include "source/common/protobuf/message_validator_impl.h"
// #include "source/common/protobuf/utility.h"
// #include "source/extensions/common/aws/aws_cluster_manager.h"
#include "source/extensions/common/aws/credentials_provider.h"
// #include "source/extensions/common/aws/metadata_fetcher.h"

// #include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

/**
 * Returns AWS credentials from static filter configuration.
 *
 * Adheres to conventions specified in:
 * https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-envvars.html
 */
class ConfigCredentialsProvider : public CredentialsProvider,
                                  public Logger::Loggable<Logger::Id::aws> {
public:
  ConfigCredentialsProvider(absl::string_view access_key_id = absl::string_view(),
                            absl::string_view secret_access_key = absl::string_view(),
                            absl::string_view session_token = absl::string_view())
      : credentials_(access_key_id, secret_access_key, session_token) {}
  Credentials getCredentials() override;
  bool credentialsPending() override { return false; };
  std::string providerName() override { return "ConfigCredentialsProvider"; };

private:
  const Credentials credentials_;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
