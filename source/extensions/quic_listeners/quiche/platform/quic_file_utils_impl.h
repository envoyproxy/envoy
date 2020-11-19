#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <vector>

#include "absl/strings/string_view.h"

namespace quic {

/**
 * Traverses the directory |dirname| and returns all of the files it contains.
 * @param dirname full path without trailing '/'.
 */
// NOLINTNEXTLINE(readability-identifier-naming)`
std::vector<std::string> ReadFileContentsImpl(const std::string& dirname);

/**
 * Reads the contents of |filename| as a string into |contents|.
 *  @param filename the full path to the file.
 *  @param contents output location of the file content.
 */
// NOLINTNEXTLINE(readability-identifier-naming)
void ReadFileContentsImpl(absl::string_view filename, std::string* contents);

} // namespace quic
