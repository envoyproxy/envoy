#pragma once

#include "envoy/common/pure.h"
#include "envoy/http/message.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class Signer {
public:
  virtual ~Signer() = default;

  /**
   * Sign an AWS request.
   * @param message an AWS API request message.
   * @param sign_body include the message body in the signature. The body must be fully buffered.
   * @param override_region override the default region that has to be used to sign the request
   * @throws EnvoyException if the request cannot be signed.
   */
  virtual absl::Status sign(Http::RequestMessage& message, bool sign_body,
                            const absl::string_view override_region = "") PURE;

  /**
   * Sign an AWS request without a payload (empty string used as content hash).
   * @param headers AWS API request headers.
   * @param override_region override the default region that has to be used to sign the request
   * @throws EnvoyException if the request cannot be signed.
   */
  virtual absl::Status signEmptyPayload(Http::RequestHeaderMap& headers,
                                        const absl::string_view override_region = "") PURE;

  /**
   * Sign an AWS request using the literal string UNSIGNED-PAYLOAD in the canonical request.
   * @param headers AWS API request headers.
   * @param override_region override the default region that has to be used to sign the request
   * @throws EnvoyException if the request cannot be signed.
   */
  virtual absl::Status signUnsignedPayload(Http::RequestHeaderMap& headers,
                                           const absl::string_view override_region = "") PURE;

  /**
   * Sign an AWS request.
   * @param headers AWS API request headers.
   * @param content_hash The Hex encoded SHA-256 of the body of the AWS API request.
   * @param override_region override the default region that has to be used to sign the request
   * @throws EnvoyException if the request cannot be signed.
   */
  virtual absl::Status sign(Http::RequestHeaderMap& headers, const std::string& content_hash,
                            const absl::string_view override_region = "") PURE;
};

using SignerPtr = std::unique_ptr<Signer>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
