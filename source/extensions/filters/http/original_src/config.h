#pragma once

#include "envoy/config/filter/http/original_src/v2alpha1/original_src.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {
class Config {
public:
  Config() = default;
  explicit Config(const envoy::config::filter::http::original_src::v2alpha1::OriginalSrc& config);

  uint32_t mark() const { return mark_; }

private:
  uint32_t mark_ = 0;
};
} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
