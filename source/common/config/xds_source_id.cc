#include "source/common/config/xds_source_id.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Config {

XdsConfigSourceId::XdsConfigSourceId(absl::string_view authority_id,
                                     absl::string_view resource_type_url)
    : authority_id_(authority_id), resource_type_url_(resource_type_url) {}

std::string XdsConfigSourceId::toKey() const {
  // The delimiter between parts of the key. The type URL cannot contain a "+" character, so we
  // don't run the risk of collisions with different parts of the key having a "+" in them.
  static constexpr char DELIMITER[] = "+";
  return absl::StrCat(authority_id_, DELIMITER, resource_type_url_);
}

} // namespace Config
} // namespace Envoy
