#pragma once

#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Aws {
namespace Auth {

class MetadataFetcher {
public:
  virtual ~MetadataFetcher() = default;

  /**
   * Fetch instance metadata from a known host.
   * @param dispatcher the dispatcher to execute the http request on.
   * @param host the instance metadata host. Example: 169.254.169.254:80
   * @param path the instance metadata path. Example: /latest/meta-data/iam/info
   * @param auth_token an optional authorization token for requesting the metdata.
   * @return an optional string containing the instance metadata if it can be found.
   */
  virtual absl::optional<std::string> getMetadata(
      Event::Dispatcher& dispatcher, const std::string& host, const std::string& path,
      const absl::optional<std::string>& auth_token = absl::optional<std::string>()) const PURE;
};

typedef std::unique_ptr<MetadataFetcher> MetadataFetcherPtr;

} // namespace Auth
} // namespace Aws
} // namespace Envoy