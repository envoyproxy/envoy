#pragma once

namespace Envoy {
template <typename T> void pushScalarToByteVector(T val, std::vector<uint8_t>& bytes) {
  uint8_t* byte_ptr = reinterpret_cast<uint8_t*>(&val);
  for (unsigned int byte_index = 0; byte_index < sizeof val; byte_index++) {
    bytes.push_back(*byte_ptr);
    byte_ptr++;
  }
}
} // namespace Envoy
