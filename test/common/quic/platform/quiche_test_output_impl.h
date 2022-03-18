#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/strings/string_view.h"

namespace quiche {
// NOLINTNEXTLINE(readability-identifier-naming)
void QuicheSaveTestOutputImpl(absl::string_view filename, absl::string_view data);

// NOLINTNEXTLINE(readability-identifier-naming)
bool QuicheLoadTestOutputImpl(absl::string_view filename, std::string* data);

// NOLINTNEXTLINE(readability-identifier-naming)
void QuicheRecordTraceImpl(absl::string_view identifier, absl::string_view data);

} // namespace quiche
