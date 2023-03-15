#pragma once

#include <string>

#include "envoy/common/pure.h"
#include "envoy/server/admin.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {
namespace AdminHtml {

/**
 * @param buf a buffer that may be used by the implementation to prepare the return value.
 * @return resource contents
 */
absl::string_view getResource(absl::string_view resource_name, std::string& buf);

// Overridable mechanism to provide resources for constructing HTML resources.
// This is used to facilitate interactive debugging by dynamically reading
// resource contents from the file system.
//
// Note: rather than creating a new interace here, we could have re-used
// Filesystem::Instance, however the current implementation of MemFileSystem
// is intended for tests, and it's simpler to create a much leaner new API
// rather than productionize the full memory-based filesystem implementation.
class HtmlResourceProvider {
 public:
  virtual ~HtmlResourceProvider() = default;

  /**
   * @param buf a buffer that may be used by the implementation to prepare the return value.
   * @return resource contents
   */
  virtual absl::string_view getResource(absl::string_view resource_name, std::string& buf) PURE;
};

void setHtmlResourceProvider(std::unique_ptr<HtmlResourceProvider> resource_provider);

} // namespace AdminHtml
} // namespace Server
} // namespace Envoy
