#include "envoy/common/exception.h"

#include "absl/strings/string_view.h"
#include "udpa/core/v1/resource_locator.pb.h"
#include "udpa/core/v1/resource_name.pb.h"

namespace Envoy {
namespace Config {

// Utilities for URI encoding/decoding of udpa::core::v1::Resource{Name,Locator}.
class UdpaResourceIdentifier {
public:
  // Options for encoded URIs.
  struct EncodeOptions {
    // Should the context params be sorted by key? This provides deterministic encoding.
    bool sort_context_params_{};
  };

  /**
   * Encode a udpa::core::v1::ResourceName message as a udpa:// URN string.
   *
   * @param resource_name resource name message.
   * @param options encoding options.
   * @return std::string udpa:// URN for resource_name.
   */
  static std::string encodeUrn(const udpa::core::v1::ResourceName& resource_name,
                               const EncodeOptions& options);
  static std::string encodeUrn(const udpa::core::v1::ResourceName& resource_name) {
    return encodeUrn(resource_name, {});
  }

  /**
   * Encode a udpa::core::v1::ResourceLocator message as a udpa:// URL string.
   *
   * @param resource_name resource name message.
   * @param options encoding options.
   * @return std::string udpa:// URL for resource_name.
   */
  static std::string encodeUrl(const udpa::core::v1::ResourceLocator& resource_locator,
                               const EncodeOptions& options);
  static std::string encodeUrl(const udpa::core::v1::ResourceLocator& resource_locator) {
    return encodeUrl(resource_locator, {});
  }

  // Thrown when an exception occurs during URI decoding.
  class DecodeException : public EnvoyException {
  public:
    DecodeException(const std::string& what) : EnvoyException(what) {}
  };

  /**
   * Decode a udpa:// URN string to a udpa::core::v1::ResourceName.
   *
   * @param resource_urn udpa:// resource URN.
   * @return udpa::core::v1::ResourceName resource name message for resource_urn.
   * @throws DecodeException when parsing fails.
   */
  static udpa::core::v1::ResourceName decodeUrn(absl::string_view resource_urn);

  /**
   * Decode a udpa:// URL string to a udpa::core::v1::ResourceLocator.
   *
   * @param resource_url udpa:// resource URL.
   * @return udpa::core::v1::ResourceLocator resource name message for resource_url.
   * @throws DecodeException when parsing fails.
   */
  static udpa::core::v1::ResourceLocator decodeUrl(absl::string_view resource_url);
};

} // namespace Config
} // namespace Envoy
