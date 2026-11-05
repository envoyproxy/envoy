#pragma once

#include "envoy/common/exception.h"

#include "absl/strings/string_view.h"
#include "xds/core/v3/resource_locator.pb.h"
#include "xds/core/v3/resource_name.pb.h"

namespace Envoy {
namespace Config {

// Utilities for URI encoding/decoding of xds::core::v3::Resource{Name,Locator}.
class XdsResourceIdentifier {
public:
  // Options for encoded URIs.
  struct EncodeOptions {
    // Should the context params be sorted by key? This provides deterministic encoding.
    bool sort_context_params_{};
  };

  /**
   * Encode a xds::core::v3::ResourceName message as a xdstp:// URN string.
   *
   * @param resource_name resource name message.
   * @param options encoding options.
   * @return std::string xdstp:// URN for resource_name.
   */
  static std::string encodeUrn(const xds::core::v3::ResourceName& resource_name,
                               const EncodeOptions& options);
  static std::string encodeUrn(const xds::core::v3::ResourceName& resource_name) {
    return encodeUrn(resource_name, {});
  }

  /**
   * Encode a xds::core::v3::ResourceLocator message as a xdstp:// URL string.
   *
   * @param resource_name resource name message.
   * @param options encoding options.
   * @return std::string xdstp:// URL for resource_name.
   */
  static std::string encodeUrl(const xds::core::v3::ResourceLocator& resource_locator,
                               const EncodeOptions& options);
  static std::string encodeUrl(const xds::core::v3::ResourceLocator& resource_locator) {
    return encodeUrl(resource_locator, {});
  }

  /**
   * Decode a xdstp:// URN string to a xds::core::v3::ResourceName.
   *
   * @param resource_urn xdstp:// resource URN.
   * @return xds::core::v3::ResourceName resource name message for resource_urn.
   * @throws EnvoyException when parsing fails.
   */
  static absl::StatusOr<xds::core::v3::ResourceName> decodeUrn(absl::string_view resource_urn);

  /**
   * Decode a xdstp:// URL string to a xds::core::v3::ResourceLocator.
   *
   * @param resource_url xdstp:// resource URL.
   * @return xds::core::v3::ResourceLocator resource name message for resource_url.
   * @throws EnvoyException when parsing fails.
   */
  static absl::StatusOr<xds::core::v3::ResourceLocator> decodeUrl(absl::string_view resource_url);

  /**
   * Normalize an xDS resource name. If the resource name has an xdstp: scheme, decode and
   * re-encode it with sorted context parameters; otherwise return it unchanged.
   *
   * @param resource_name resource name.
   * @return absl::StatusOr<std::string> normalized resource name, or an error status if decoding
   *         an xdstp: URN fails.
   */
  static absl::StatusOr<std::string> normalizeResourceName(absl::string_view resource_name);

  /**
   * @param resource_name resource name.
   * @return bool does resource_name have a xdstp: scheme?
   */
  static bool hasXdsTpScheme(absl::string_view resource_name);
};

} // namespace Config
} // namespace Envoy
