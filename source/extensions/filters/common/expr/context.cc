#include "extensions/filters/common/expr/context.h"

#include "absl/strings/numbers.h"
#include "absl/time/time.h"

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
  } else if (value == Time) {
    return CelValue::CreateTimestamp(absl::FromChrono(info_.startTime()));
  } else if (value == ID) {
    return convertHeaderEntry(wrapper_.headers_.RequestId());
  } else if (value == UserAgent) {
    return convertHeaderEntry(wrapper_.headers_.UserAgent());
  } else if (value == Size) {
    // it is important to make a choice whether to rely on content-length vs stream info
    // (which is not available at the time of the request headers)
    auto length_header = wrapper_.headers_.ContentLength();
    if (length_header != nullptr) {
      int64_t length;
      if (absl::SimpleAtoi(length_header->value().getStringView(), &length)) {
        return CelValue::CreateInt64(length);
      }
    }
  } else if (value == TotalSize) {
    return CelValue::CreateInt64(info_.bytesReceived() + wrapper_.headers_.byteSize());
  }
  return {};
}

absl::optional<CelValue> ConnectionWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == LocalAddress) {
    return CelValue::CreateString(info_.downstreamLocalAddress()->asStringView());
  } else if (value == RemoteAddress) {
    return CelValue::CreateString(info_.downstreamRemoteAddress()->asStringView());
  }

  return {};
}

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
