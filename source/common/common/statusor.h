#pragma once

#include "third_party/statusor/statusor.h"

/**
 * Facility for returning either a valid value or an error in a form of Envoy::Status.
 *
 * IMPORTANT: StatusOr default constructor must not be used as it does not fit into any
 * Envoy's use cases. The error extracting functions in the common/common/status.h will
 * RELEASE_ASSERT on default initialized StatusOr.
 *
 * To return an error StatusOr object an error creating function from common/common/status.h must be
 * used.
 * TODO(yanavlasov): add clang-tidy or lint check to enforce this.
 *
 * Usage example:
 *
 *  Envoy::StatusOr<int> Foo() {
 *    ...
 *    if (codec_error) {
 *      return CodecProtocolError("Invalid protocol");
 *    }
 *    return 123456;
 *  }
 *
 *  void Bar() {
 *    auto status_or = Foo();
 *    if (status_or.ok()) {
 *      int result = status_or.value();
 *      ...
 *    } else {
 *      ASSERT(IsCodecProtocolError(status_or.status()));
 *      ENVOY_LOG(debug, "Codec error encountered: {}", status_or.status().message());
 *    }
 *  }
 */

namespace Envoy {

using absl::StatusOr; // NOLINT(misc-unused-using-decls)

} // namespace Envoy
