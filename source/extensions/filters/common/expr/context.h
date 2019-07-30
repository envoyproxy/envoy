#pragma once

#include "envoy/stream_info/stream_info.h"

#include "common/http/headers.h"

#include "eval/public/cel_value.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

using CelValue = google::api::expr::runtime::CelValue;

// Symbols for traversing the request properties in the expressions
constexpr absl::string_view Request = "request";
constexpr absl::string_view Path = "path";
constexpr absl::string_view Host = "host";
constexpr absl::string_view Scheme = "scheme";
constexpr absl::string_view Method = "method";
constexpr absl::string_view Referer = "referer";
constexpr absl::string_view Headers = "headers";
constexpr absl::string_view Time = "time";
constexpr absl::string_view ID = "id";
constexpr absl::string_view UserAgent = "useragent";
constexpr absl::string_view Size = "size";
constexpr absl::string_view TotalSize = "total_size";

// Per-request metadata
constexpr absl::string_view Metadata = "metadata";

// Downstream connection properties
constexpr absl::string_view Connection = "connection";
constexpr absl::string_view LocalAddress = "local_address";
constexpr absl::string_view RemoteAddress = "remote_address";

class RequestWrapper;

class HeadersWrapper : public google::api::expr::runtime::CelMap {
public:
  HeadersWrapper(const Http::HeaderMap& headers) : headers_(headers) {}
  absl::optional<CelValue> operator[](CelValue key) const override;
  int size() const override { return headers_.size(); }
  bool empty() const override { return headers_.empty(); }
  const google::api::expr::runtime::CelList* ListKeys() const override { return nullptr; }

private:
  friend class RequestWrapper;
  const Http::HeaderMap& headers_;
};

class BaseWrapper : public google::api::expr::runtime::CelMap {
public:
  int size() const override { return 0; }
  bool empty() const override { return false; }
  const google::api::expr::runtime::CelList* ListKeys() const override { return nullptr; }
};

class RequestWrapper : public BaseWrapper {
public:
  RequestWrapper(const Http::HeaderMap& headers, const StreamInfo::StreamInfo& info)
      : wrapper_(headers), info_(info) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

private:
  const HeadersWrapper wrapper_;
  const StreamInfo::StreamInfo& info_;
};

class ConnectionWrapper : public BaseWrapper {
public:
  ConnectionWrapper(const StreamInfo::StreamInfo& info) : info_(info) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

private:
  const StreamInfo::StreamInfo& info_;
}

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
