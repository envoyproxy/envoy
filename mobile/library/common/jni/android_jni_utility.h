#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace JNI {

/**
 * For android, calls up through JNI to see if cleartext is permitted for this host.
 * For other platforms simply returns true.
 */
bool isCleartextPermitted(absl::string_view hostname);

/**
 * For android, calls up through JNI to apply host. For other platforms simply returns true.
 */
void tagSocket(int ifd, int uid, int tag);

} // namespace JNI
} // namespace Envoy
