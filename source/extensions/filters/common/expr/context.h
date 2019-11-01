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

// Symbols for traversing the request properties
constexpr absl::string_view Request = "request";
constexpr absl::string_view Path = "path";
constexpr absl::string_view UrlPath = "url_path";
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
constexpr absl::string_view Duration = "duration";

// Symbols for traversing the response properties
constexpr absl::string_view Response = "response";
constexpr absl::string_view Code = "code";
constexpr absl::string_view Trailers = "trailers";

// Per-request or per-connection metadata
constexpr absl::string_view Metadata = "metadata";

// Connection properties
constexpr absl::string_view Connection = "connection";
constexpr absl::string_view MTLS = "mtls";
constexpr absl::string_view RequestedServerName = "requested_server_name";
constexpr absl::string_view TLSVersion = "tls_version";
constexpr absl::string_view SubjectLocalCertificate = "subject_local_certificate";
constexpr absl::string_view SubjectPeerCertificate = "subject_peer_certificate";
constexpr absl::string_view URISanLocalCertificate = "uri_san_local_certificate";
constexpr absl::string_view URISanPeerCertificate = "uri_san_peer_certificate";
constexpr absl::string_view DNSSanLocalCertificate = "dns_san_local_certificate";
constexpr absl::string_view DNSSanPeerCertificate = "dns_san_peer_certificate";

// Source properties
constexpr absl::string_view Source = "source";
constexpr absl::string_view Address = "address";
constexpr absl::string_view Port = "port";

// Destination properties
constexpr absl::string_view Destination = "destination";

// Upstream properties
constexpr absl::string_view Upstream = "upstream";

class RequestWrapper;

class HeadersWrapper : public google::api::expr::runtime::CelMap {
public:
  HeadersWrapper(const Http::HeaderMap* value) : value_(value) {}
  absl::optional<CelValue> operator[](CelValue key) const override;
  int size() const override { return value_ == nullptr ? 0 : value_->size(); }
  bool empty() const override { return value_ == nullptr ? true : value_->empty(); }
  const google::api::expr::runtime::CelList* ListKeys() const override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

private:
  friend class RequestWrapper;
  const Http::HeaderMap* value_;
};

class BaseWrapper : public google::api::expr::runtime::CelMap {
public:
  int size() const override { return 0; }
  bool empty() const override { return false; }
  const google::api::expr::runtime::CelList* ListKeys() const override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
};

class RequestWrapper : public BaseWrapper {
public:
  RequestWrapper(const Http::HeaderMap* headers, const StreamInfo::StreamInfo& info)
      : headers_(headers), info_(info) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

private:
  const HeadersWrapper headers_;
  const StreamInfo::StreamInfo& info_;
};

class ResponseWrapper : public BaseWrapper {
public:
  ResponseWrapper(const Http::HeaderMap* headers, const Http::HeaderMap* trailers,
                  const StreamInfo::StreamInfo& info)
      : headers_(headers), trailers_(trailers), info_(info) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

private:
  const HeadersWrapper headers_;
  const HeadersWrapper trailers_;
  const StreamInfo::StreamInfo& info_;
};

class ConnectionWrapper : public BaseWrapper {
public:
  ConnectionWrapper(const StreamInfo::StreamInfo& info) : info_(info) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

private:
  const StreamInfo::StreamInfo& info_;
};

class UpstreamWrapper : public BaseWrapper {
public:
  UpstreamWrapper(const StreamInfo::StreamInfo& info) : info_(info) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

private:
  const StreamInfo::StreamInfo& info_;
};

class PeerWrapper : public BaseWrapper {
public:
  PeerWrapper(const StreamInfo::StreamInfo& info, bool local) : info_(info), local_(local) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

private:
  const StreamInfo::StreamInfo& info_;
  const bool local_;
};

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
