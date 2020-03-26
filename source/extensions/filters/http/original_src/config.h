#pragma once

#include "envoy/extensions/filters/http/original_src/v3/original_src.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {
class Config {
public:
  Config() = default;
  explicit Config(const envoy::extensions::filters::http::original_src::v3::OriginalSrc& config);

  uint32_t mark() const { return mark_; }

private:
  uint32_t mark_ = 0;
};
} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
