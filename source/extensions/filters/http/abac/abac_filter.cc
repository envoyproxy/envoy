#include "extensions/filters/http/abac/abac_filter.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ABACFilter {

using CelValue = google::api::expr::runtime::CelValue;
using CelMap = google::api::expr::runtime::CelMap;
using CelList = google::api::expr::runtime::CelList;

namespace {

constexpr absl::string_view kMetadata = "metadata";

constexpr absl::string_view kConnection = "connection";
constexpr absl::string_view kRequestedServerName = "requested_server_name";
constexpr absl::string_view kLocalAddress = "local_address";
constexpr absl::string_view kRemoteAddress = "remote_address";
constexpr absl::string_view kCertificatePresented = "certificate_presented";
constexpr absl::string_view kSubjectLocalCertificate = "subject_local_certificate";
constexpr absl::string_view kIssuerPeerCertificate = "issuer_peer_certificate";
constexpr absl::string_view kSubjectPeerCertificate = "subject_peer_certificate";
constexpr absl::string_view kTlsVersion = "tls_version";

constexpr absl::string_view kRequest = "request";
constexpr absl::string_view kPath = "path";
constexpr absl::string_view kHost = "host";
constexpr absl::string_view kScheme = "scheme";
constexpr absl::string_view kMethod = "method";
constexpr absl::string_view kReferer = "referer";
constexpr absl::string_view kHeaders = "headers";
constexpr absl::string_view kTime = "time";

class ConnectionWrapper : public CelMap {
public:
  ConnectionWrapper(const StreamInfo::StreamInfo& info) : info_(info) {}

  absl::optional<CelValue> operator[](CelValue key) const override {
    if (!key.IsString()) {
      return {};
    }
    auto value = key.StringOrDie().value();
    if (value == kRequestedServerName) {
      return CelValue::CreateString(info_.requestedServerName());
    } else if (value == kLocalAddress) {
      return CelValue::CreateString(info_.downstreamLocalAddress()->asString());
    } else if (value == kRemoteAddress) {
      return CelValue::CreateString(info_.downstreamRemoteAddress()->asString());
    }

    auto ssl_info = info_.downstreamSslConnection();
    if (ssl_info != nullptr) {
      if (value == kCertificatePresented) {
        return CelValue::CreateBool(ssl_info->peerCertificatePresented());
      } else if (value == kSubjectLocalCertificate) {
        return CelValue::CreateString(ssl_info->subjectLocalCertificate());
      } else if (value == kIssuerPeerCertificate) {
        return CelValue::CreateString(ssl_info->issuerPeerCertificate());
      } else if (value == kSubjectPeerCertificate) {
        return CelValue::CreateString(ssl_info->subjectPeerCertificate());
      } else if (value == kTlsVersion) {
        return CelValue::CreateString(ssl_info->tlsVersion());
      }
    }
    return {};
  }
  int size() const override { return 0; }
  bool empty() const override { return false; }
  const CelList* ListKeys() const override { return nullptr; }

private:
  const StreamInfo::StreamInfo& info_;
};

class HeadersWrapper : public CelMap {
public:
  HeadersWrapper(const Http::HeaderMap& headers) : headers_(headers) {}

  absl::optional<CelValue> operator[](CelValue key) const override {
    if (!key.IsString()) {
      return {};
    }
    auto out = headers_.get(Http::LowerCaseString(std::string(key.StringOrDie().value())));
    if (out == nullptr) {
      return {};
    }
    return CelValue::CreateString(out->value().getStringView());
  }
  int size() const override { return headers_.size(); }
  bool empty() const override { return headers_.empty(); }
  const CelList* ListKeys() const override { return nullptr; }
  const Http::HeaderMap& headers_;
};

class RequestWrapper : public CelMap {
public:
  RequestWrapper(const Http::HeaderMap& headers, const StreamInfo::StreamInfo& info)
      : wrapper_(headers), info_(info) {}

  absl::optional<CelValue> operator[](CelValue key) const override {
    if (!key.IsString()) {
      return {};
    }
    auto value = key.StringOrDie().value();
    if (value == kPath) {
      return CelValue::CreateString(wrapper_.headers_.Path()->value().getStringView());
    } else if (value == kHost) {
      return CelValue::CreateString(wrapper_.headers_.Host()->value().getStringView());
    } else if (value == kScheme) {
      return CelValue::CreateString(wrapper_.headers_.Scheme()->value().getStringView());
    } else if (value == kMethod) {
      return CelValue::CreateString(wrapper_.headers_.Method()->value().getStringView());
    } else if (value == kReferer) {
      return CelValue::CreateString(wrapper_.headers_.Referer()->value().getStringView());
    } else if (value == kHeaders) {
      return CelValue::CreateMap(&wrapper_);
    } else if (value == kTime) {
      return CelValue::CreateTimestamp(absl::FromChrono(info_.startTime()));
    }
    return {};
  }

  int size() const override { return 0; }
  bool empty() const override { return false; }
  const CelList* ListKeys() const override { return nullptr; }

private:
  const HeadersWrapper wrapper_;
  const StreamInfo::StreamInfo& info_;
};

} // namespace

Http::FilterHeadersStatus AttributeBasedAccessControlFilter::decodeHeaders(Http::HeaderMap& headers,
                                                                           bool) {
  Protobuf::Arena arena;
  google::api::expr::runtime::Activation activation;
  activation.InsertValue(
      kMetadata, CelValue::CreateMessage(&callbacks_->streamInfo().dynamicMetadata(), &arena));
  const ConnectionWrapper connection(callbacks_->streamInfo());
  activation.InsertValue(kConnection, CelValue::CreateMap(&connection));
  const RequestWrapper request(headers, callbacks_->streamInfo());
  activation.InsertValue(kRequest, CelValue::CreateMap(&request));

  auto eval_status = expr_->Evaluate(activation, &arena);
  if (!eval_status.ok()) {
    ENVOY_LOG(debug, "evaluation failed: {}", eval_status.status().message());
    return Http::FilterHeadersStatus::StopIteration;
  }

  auto result = eval_status.ValueOrDie();
  if (result.IsBool() && result.BoolOrDie()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (result.IsError()) {
    ENVOY_LOG(debug, "evaluation error: {}", result.ErrorOrDie()->message());
  } else if (result.IsString()) {
    ENVOY_LOG(debug, "evaluation resulted in a string: {}", result.StringOrDie().value());
  } else {
    ENVOY_LOG(debug, "expression did not evaluate to 'true': type {}",
              CelValue::TypeName(result.type()));
  }
  return Http::FilterHeadersStatus::StopIteration;
}

} // namespace ABACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
