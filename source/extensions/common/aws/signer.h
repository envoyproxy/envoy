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
   * @throws EnvoyException if the request cannot be signed.
   */
  virtual void sign(Http::RequestMessage& message, bool sign_body) PURE;

  /**
   * Sign an AWS request.
   * @param headers AWS API request headers.
   * @throws EnvoyException if the request cannot be signed.
   */
  virtual void sign(Http::RequestHeaderMap& headers) PURE;

  /**
   * Sign an AWS request.
   * @param headers AWS API request headers.
   * @param content_hash The Hex encoded SHA-256 of the body of the AWS API request.
   * @throws EnvoyException if the request cannot be signed.
   */
  virtual void sign(Http::RequestHeaderMap& headers, const std::string& content_hash) PURE;
};

using SignerPtr = std::unique_ptr<Signer>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
