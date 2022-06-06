#pragma once

#include "absl/strings/string_view.h"

// NOLINT(namespace-envoy)

/* For android, calls up through JNI to see if cleartext is permitted for this
 * host.
 * For other platforms simply returns true.
 */
bool is_cleartext_permitted(absl::string_view hostname);
