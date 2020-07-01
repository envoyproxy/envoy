#include "envoy/common/exception.h"

#include "absl/strings/string_view.h"
#include "udpa/core/v1/resource_name.pb.h"

namespace Envoy {
namespace Config {

// Utilities for URI encoding/decoding of udpa::core::v1::ResourceName.
class UdpaResourceName {
public:
  // Options for encoded URIs.
  struct EncodeOptions {
    // Should the context params be sorted by key? This provides deterministic encoding.
    bool sort_context_params_{};
  };

  /**
   * Encode a udpa::core::v1::ResourceName message as a udpa:// URI string.
   *
   * @param resource_name resource name message.
   * @param options encoding options.
   * @return std::string udpa:// URI for resource_name.
   */
  static std::string encodeUri(const udpa::core::v1::ResourceName& resource_name,
                               const EncodeOptions& options);
  static std::string encodeUri(const udpa::core::v1::ResourceName& resource_name) {
    return encodeUri(resource_name, {});
  }

  // Thrown when an exception occurs during URI decoding.
  class DecodeException : public EnvoyException {
  public:
    DecodeException(const std::string& what) : EnvoyException(what) {}
  };

  /**
   * Decode a udpa:// URI string to a udpa::core::v1::ResourceName.
   *
   * @param resource_uri udpa:// resource URI.
   * @return udpa::core::v1::ResourceName resource name message for resource_uri.
   * @throws DecodeException when parsing fails.
   */
  static udpa::core::v1::ResourceName decodeUri(absl::string_view resource_uri);
};

} // namespace Config
} // namespace Envoy
