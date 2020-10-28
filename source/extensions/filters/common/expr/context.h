#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/stream_info/stream_info.h"

#include "common/grpc/status.h"
#include "common/http/header_utility.h"
#include "common/http/headers.h"

#include "eval/public/cel_value.h"
#include "eval/public/cel_value_producer.h"

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
constexpr absl::string_view Protocol = "protocol";

// Symbols for traversing the response properties
constexpr absl::string_view Response = "response";
constexpr absl::string_view Code = "code";
constexpr absl::string_view CodeDetails = "code_details";
constexpr absl::string_view Trailers = "trailers";
constexpr absl::string_view Flags = "flags";
constexpr absl::string_view GrpcStatus = "grpc_status";

// Per-request or per-connection metadata
constexpr absl::string_view Metadata = "metadata";

// Per-request or per-connection filter state
constexpr absl::string_view FilterState = "filter_state";

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
constexpr absl::string_view UpstreamLocalAddress = "local_address";
constexpr absl::string_view UpstreamTransportFailureReason = "transport_failure_reason";

class RequestWrapper;

absl::optional<CelValue> convertHeaderEntry(const Http::HeaderEntry* header);
absl::optional<CelValue>
convertHeaderEntry(Protobuf::Arena& arena,
                   Http::HeaderUtility::GetAllOfHeaderAsStringResult&& result);

template <class T> class HeadersWrapper : public google::api::expr::runtime::CelMap {
public:
  HeadersWrapper(Protobuf::Arena& arena, const T* value) : arena_(arena), value_(value) {}
  absl::optional<CelValue> operator[](CelValue key) const override {
    if (value_ == nullptr || !key.IsString()) {
      return {};
    }
    auto str = std::string(key.StringOrDie().value());
    if (!Http::validHeaderString(str)) {
      // Reject key if it is an invalid header string
      return {};
    }
    return convertHeaderEntry(
        arena_, Http::HeaderUtility::getAllOfHeaderAsString(*value_, Http::LowerCaseString(str)));
  }
  int size() const override { return value_ == nullptr ? 0 : value_->size(); }
  bool empty() const override { return value_ == nullptr ? true : value_->empty(); }
  const google::api::expr::runtime::CelList* ListKeys() const override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

private:
  friend class RequestWrapper;
  friend class ResponseWrapper;
  Protobuf::Arena& arena_;
  const T* value_;
};

// Wrapper for accessing properties from internal data structures.
// Note that CEL assumes no ownership of the underlying data, so temporary
// data must be arena-allocated.
class BaseWrapper : public google::api::expr::runtime::CelMap,
                    public google::api::expr::runtime::CelValueProducer {
public:
  int size() const override { return 0; }
  bool empty() const override { return false; }
  const google::api::expr::runtime::CelList* ListKeys() const override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  CelValue Produce(ProtobufWkt::Arena* arena) override {
    // Producer is unique per evaluation arena since activation is re-created.
    arena_ = arena;
    return CelValue::CreateMap(this);
  }

protected:
  ProtobufWkt::Arena* arena_;
};

class RequestWrapper : public BaseWrapper {
public:
  RequestWrapper(Protobuf::Arena& arena, const Http::RequestHeaderMap* headers,
                 const StreamInfo::StreamInfo& info)
      : headers_(arena, headers), info_(info) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

private:
  const HeadersWrapper<Http::RequestHeaderMap> headers_;
  const StreamInfo::StreamInfo& info_;
};

class ResponseWrapper : public BaseWrapper {
public:
  ResponseWrapper(Protobuf::Arena& arena, const Http::ResponseHeaderMap* headers,
                  const Http::ResponseTrailerMap* trailers, const StreamInfo::StreamInfo& info)
      : headers_(arena, headers), trailers_(arena, trailers), info_(info) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

private:
  const HeadersWrapper<Http::ResponseHeaderMap> headers_;
  const HeadersWrapper<Http::ResponseTrailerMap> trailers_;
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

class MetadataProducer : public google::api::expr::runtime::CelValueProducer {
public:
  MetadataProducer(const envoy::config::core::v3::Metadata& metadata) : metadata_(metadata) {}
  CelValue Produce(ProtobufWkt::Arena* arena) override {
    return CelValue::CreateMessage(&metadata_, arena);
  }

private:
  const envoy::config::core::v3::Metadata& metadata_;
};

class FilterStateWrapper : public BaseWrapper {
public:
  FilterStateWrapper(const StreamInfo::FilterState& filter_state) : filter_state_(filter_state) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

private:
  const StreamInfo::FilterState& filter_state_;
};

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
