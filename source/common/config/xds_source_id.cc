#include "source/common/config/xds_source_id.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Config {

XdsConfigSourceId::XdsConfigSourceId(absl::string_view authority_id,
                                     absl::string_view resource_type_url)
    : authority_id_(authority_id), resource_type_url_(resource_type_url) {}

std::string XdsConfigSourceId::toKey() const {
  // The delimiter between parts of the key.
  static constexpr char DELIMITER[] = "+";

  // Base64 URL-encoding the authority_id and resource_type_url so that the resulting string does
  // not contain a "+" character, so that we can use it as a delimiter.
  return absl::StrCat(Base64Url::encode(authority_id_.data(), authority_id_.size()), DELIMITER,
                      Base64Url::encode(resource_type_url_.data(), resource_type_url_.size()));
}

} // namespace Config
} // namespace Envoy
