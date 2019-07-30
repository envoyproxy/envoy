#include "extensions/filters/common/expr/context.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

namespace {

absl::optional<CelValue> convertHeaderEntry(const Http::HeaderEntry* header) {
  if (header == nullptr) {
    return {};
  }
  return CelValue::CreateString(header->value().getStringView());
}

} // namespace

absl::optional<CelValue> HeadersWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto out = headers_.get(Http::LowerCaseString(std::string(key.StringOrDie().value())));
  return convertHeaderEntry(out);
}

absl::optional<CelValue> RequestWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == Path) {
    return convertHeaderEntry(wrapper_.headers_.Path());
  } else if (value == Host) {
    return convertHeaderEntry(wrapper_.headers_.Host());
  } else if (value == Scheme) {
    return convertHeaderEntry(wrapper_.headers_.Scheme());
  } else if (value == Method) {
    return convertHeaderEntry(wrapper_.headers_.Method());
  } else if (value == Referer) {
    return convertHeaderEntry(wrapper_.headers_.Referer());
  } else if (value == Headers) {
    return CelValue::CreateMap(&wrapper_);
    // time of the request?
  } else if (value == Time) {
    return CelValue::CreateTimestamp(absl::FromChrono(info_.startTime()));
  } else if (value == ID) {
    return convertHeaderEntry(wrapper_.headers_.RequestId());
  } else if (value == UserAgent) {
    return convertHeaderEntry(wrapper_.headers_.UserAgent());
  }
  // size = content length
  // useragent
  return {};
}

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
