#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quic {

// Traverses the directory |dirname| and returns all of the files it contains.
std::vector<std::string> ReadFileContentsImpl(const std::string& dirname);

// Reads the contents of |filename| as a string into |contents|.
void ReadFileContentsImpl(QuicStringPiece filename, std::string* contents);

}  // namespace quic
