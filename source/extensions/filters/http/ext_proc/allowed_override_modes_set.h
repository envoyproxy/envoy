#pragma once

#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.pb.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

/**
 * A data-structure that holds the Allowed Override Mode set.
 * Internally it converts each allowed-override-mode enums values to a single
 * list. This saves some memory, and allows an O(1) lookup.
 */
class AllowedOverrideModesSet {
public:
  /**
   * Constructs an AllowedOverrideModesSet from a container of ProcessingMode protos.
   *
   * Each ProcessingMode in the input container is converted to an integer key
   * and stored in an internal hash set for efficient lookup.
   * The use of a template is to simplify passing any container that has
   * envoy::extensions::filters::http::ext_proc::v3::ProcessingMode elements.
   *
   * @param modes The collection of ProcessingMode protos to initialize the set with.
   */
  template <typename Container> explicit AllowedOverrideModesSet(const Container& modes) {
    for (const auto& mode : modes) {
      allowed_modes_.insert(processingModeToInt(mode));
    }
  }

  /**
   * Checks if a specific ProcessingMode is supported (i.e., present in the set).
   * Note that the ``request_header_mode`` in the given mode will be ignored.
   *
   * @param mode The ProcessingMode object to check for support.
   * @return True if the mode is supported, false otherwise.
   */
  bool isModeSupported(
      const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& mode) const {
    // Convert the input ProcessingMode to its integer key representation and
    // check if this key exists in the internal hash set.
    return allowed_modes_.contains(processingModeToInt(mode));
  }

  /**
   * Checks if the set of allowed override modes is empty.
   * @return True if the set is empty, false otherwise.
   */
  bool empty() const { return allowed_modes_.empty(); }

private:
  /**
   * Converts a ProcessingMode proto to a unique integer key.
   *
   * This method maps each relevant field of the ProcessingMode (response_header_mode,
   * request_body_mode, response_body_mode, request_trailer_mode, response_trailer_mode)
   * to a single digit in a base-10 number, where each digit's value is the integral
   * value of the corresponding enum.
   *
   * The `request_header_mode` field is explicitly ignored in this conversion
   * (mapped to 0) to preserve existing filter behavior where this field is
   * not considered during mode comparison.
   *
   * Example:
   *  response_header_mode = SEND (1)
   *  request_body_mode = BUFFERED (2)
   *  ...
   *  Results in a key like: ...21 (where 1 is for response_header_mode, 2 for request_body_mode)
   *
   * @param mode The ProcessingMode proto to convert.
   * @return A uint32_t representing the unique integer key for the mode.
   */
  static uint32_t
  processingModeToInt(const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& mode) {
    uint32_t key = 0;
    // Field 1: request_header_mode (Index 1 -> 10^0). This is ignored!
    // Field 2: response_header_mode (Index 2 -> 10^1)
    key += static_cast<uint32_t>(mode.response_header_mode()) * 10;
    // Field 3: request_body_mode (Index 3 -> 10^2)
    key += static_cast<uint32_t>(mode.request_body_mode()) * 100;
    // Field 4: response_body_mode (Index 4 -> 10^3)
    key += static_cast<uint32_t>(mode.response_body_mode()) * 1000;
    // Field 5: request_trailer_mode (Index 5 -> 10^4)
    key += static_cast<uint32_t>(mode.request_trailer_mode()) * 10000;
    // Field 6: response_trailer_mode (Index 6 -> 10^5)
    key += static_cast<uint32_t>(mode.response_trailer_mode()) * 100000;
    return key;
  }

  absl::flat_hash_set<uint32_t> allowed_modes_;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
