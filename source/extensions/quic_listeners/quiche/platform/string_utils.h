// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"

namespace quiche {

void Base64Encode(const uint8_t* data, size_t data_len, std::string* output);

std::string HexDump(absl::string_view data);

// '0' => 0,  '1' => 1, 'a' or 'A' => 10, etc.
char HexDigitToInt(char c);

// Turns a 8-byte hex string into a uint32 in host byte order.
// e.g. "12345678" => 0x12345678
bool HexDecodeToUInt32(absl::string_view data, uint32_t* out);

} // namespace quiche
