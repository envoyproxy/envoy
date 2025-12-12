#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/grpc/status.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/singleton/const_singleton.h"
#include "source/common/stream_info/utility.h"

#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_list_impl.h"
#include "eval/public/structs/cel_proto_wrapper.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

using CelValue = google::api::expr::runtime::CelValue;
using CelProtoWrapper = google::api::expr::runtime::CelProtoWrapper;

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
constexpr absl::string_view Query = "query";

// Symbols for traversing the response properties
constexpr absl::string_view Response = "response";
constexpr absl::string_view Code = "code";
constexpr absl::string_view CodeDetails = "code_details";
constexpr absl::string_view Trailers = "trailers";
constexpr absl::string_view Flags = "flags";
constexpr absl::string_view GrpcStatus = "grpc_status";
constexpr absl::string_view BackendLatency = "backend_latency";

// Per-request or per-connection metadata
constexpr absl::string_view Metadata = "metadata";

// Per-request or per-connection filter state
constexpr absl::string_view FilterState = "filter_state";
constexpr absl::string_view UpstreamFilterState = "upstream_filter_state";

// Connection properties
constexpr absl::string_view Connection = "connection";
constexpr absl::string_view MTLS = "mtls";
constexpr absl::string_view RequestedServerName = "requested_server_name";
constexpr absl::string_view TLSVersion = "tls_version";
constexpr absl::string_view ConnectionTerminationDetails = "termination_details";
constexpr absl::string_view SubjectLocalCertificate = "subject_local_certificate";
constexpr absl::string_view SubjectPeerCertificate = "subject_peer_certificate";
constexpr absl::string_view URISanLocalCertificate = "uri_san_local_certificate";
constexpr absl::string_view URISanPeerCertificate = "uri_san_peer_certificate";
constexpr absl::string_view DNSSanLocalCertificate = "dns_san_local_certificate";
constexpr absl::string_view DNSSanPeerCertificate = "dns_san_peer_certificate";
constexpr absl::string_view SHA256PeerCertificateDigest = "sha256_peer_certificate_digest";
constexpr absl::string_view DownstreamTransportFailureReason = "transport_failure_reason";

// Source properties
constexpr absl::string_view Source = "source";
constexpr absl::string_view Address = "address";
constexpr absl::string_view Port = "port";

// Destination properties
constexpr absl::string_view Destination = "destination";

// Upstream properties
constexpr absl::string_view Upstream = "upstream";
constexpr absl::string_view UpstreamLocalAddress = "local_address";
constexpr absl::string_view UpstreamLocality = "locality";
constexpr absl::string_view UpstreamTransportFailureReason = "transport_failure_reason";
constexpr absl::string_view UpstreamRequestAttemptCount = "request_attempt_count";
constexpr absl::string_view UpstreamConnectionPoolReadyDuration = "cx_pool_ready_duration";

// xDS configuration context properties
constexpr absl::string_view XDS = "xds";
constexpr absl::string_view ClusterName = "cluster_name";
constexpr absl::string_view ClusterMetadata = "cluster_metadata";
constexpr absl::string_view RouteName = "route_name";
constexpr absl::string_view RouteMetadata = "route_metadata";
constexpr absl::string_view VirtualHostName = "virtual_host_name";
constexpr absl::string_view VirtualHostMetadata = "virtual_host_metadata";
constexpr absl::string_view UpstreamHostMetadata = "upstream_host_metadata";
constexpr absl::string_view UpstreamHostLocalityMetadata = "upstream_host_locality_metadata";
constexpr absl::string_view FilterChainName = "filter_chain_name";
constexpr absl::string_view ListenerMetadata = "listener_metadata";
constexpr absl::string_view ListenerDirection = "listener_direction";
constexpr absl::string_view Node = "node";

class WrapperFieldValues {
public:
  using ContainerBackedListImpl = google::api::expr::runtime::ContainerBackedListImpl;
  const ContainerBackedListImpl Empty{{}};
};

using WrapperFields = ConstSingleton<WrapperFieldValues>;

// Forward declarations
class RequestWrapper;
class ResponseWrapper;
class ConnectionWrapper;
class UpstreamWrapper;
class PeerWrapper;
class FilterStateWrapper;
class XDSWrapper;

// Type aliases for value extractors
using CelValueExtractor = std::function<absl::optional<CelValue>(const RequestWrapper&)>;
using ResponseValueExtractor = std::function<absl::optional<CelValue>(const ResponseWrapper&)>;
using ConnectionValueExtractor = std::function<absl::optional<CelValue>(const ConnectionWrapper&)>;
using UpstreamValueExtractor = std::function<absl::optional<CelValue>(const UpstreamWrapper&)>;
using XDSValueExtractor = std::function<absl::optional<CelValue>(const XDSWrapper&)>;
using SslExtractor = std::function<absl::optional<CelValue>(const Ssl::ConnectionInfo&)>;

// Forward declare the singleton value classes
class RequestLookupValues {
public:
  const absl::flat_hash_map<absl::string_view, CelValueExtractor> request_lookup_;
  static const RequestLookupValues& get();
};

class ResponseLookupValues {
public:
  const absl::flat_hash_map<absl::string_view, ResponseValueExtractor> response_lookup_;
  static const ResponseLookupValues& get();
};

class ConnectionLookupValues {
public:
  const absl::flat_hash_map<absl::string_view, ConnectionValueExtractor> connection_lookup_;
  static const ConnectionLookupValues& get();
};

class UpstreamLookupValues {
public:
  const absl::flat_hash_map<absl::string_view, UpstreamValueExtractor> upstream_lookup_;
  static const UpstreamLookupValues& get();
};

class XDSLookupValues {
public:
  const absl::flat_hash_map<absl::string_view, XDSValueExtractor> xds_lookup_;
  static const XDSLookupValues& get();
};

class SslExtractorsValues {
public:
  const absl::flat_hash_map<absl::string_view, SslExtractor> extractors_;
  static const SslExtractorsValues& get();
};

// Helper functions declarations
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
    if (!Http::HeaderUtility::headerNameIsValid(str)) {
      // Reject key if it is an invalid header string
      return {};
    }
    return convertHeaderEntry(arena_, ::Envoy::Http::HeaderUtility::getAllOfHeaderAsString(
                                          *value_, ::Envoy::Http::LowerCaseString(str)));
  }
  int size() const override { return ListKeys().value()->size(); }
  bool empty() const override { return value_ == nullptr ? true : value_->empty(); }
  using CelMap::ListKeys;
  absl::StatusOr<const google::api::expr::runtime::CelList*> ListKeys() const override {
    if (value_ == nullptr) {
      return &WrapperFields::get().Empty;
    }
    absl::flat_hash_set<absl::string_view> keys;
    value_->iterate(
        [&keys](const ::Envoy::Http::HeaderEntry& header) -> ::Envoy::Http::HeaderMap::Iterate {
          keys.insert(header.key().getStringView());
          return ::Envoy::Http::HeaderMap::Iterate::Continue;
        });
    std::vector<CelValue> values;
    values.reserve(keys.size());
    for (const auto& key : keys) {
      values.push_back(CelValue::CreateStringView(key));
    }
    return Protobuf::Arena::Create<google::api::expr::runtime::ContainerBackedListImpl>(&arena_,
                                                                                        values);
  }

protected:
  friend class RequestLookupValues;
  friend class ResponseLookupValues;
  Protobuf::Arena& arena_;
  const T* value_;
};

// Wrapper for accessing properties from internal data structures.
// Note that CEL assumes no ownership of the underlying data, so temporary
// data must be arena-allocated.
class BaseWrapper : public google::api::expr::runtime::CelMap {
public:
  BaseWrapper(Protobuf::Arena& arena) : arena_(arena) {}
  int size() const override { return 0; }
  using CelMap::ListKeys;
  absl::StatusOr<const google::api::expr::runtime::CelList*> ListKeys() const override {
    return absl::UnimplementedError("ListKeys() is not implemented");
  }

protected:
  ProtobufWkt::Arena& arena_;
};

class RequestWrapper : public BaseWrapper {
public:
  RequestWrapper(Protobuf::Arena& arena, const ::Envoy::Http::RequestHeaderMap* headers,
                 const StreamInfo::StreamInfo& info)
      : BaseWrapper(arena), headers_(arena, headers), info_(info) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

protected:
  friend class RequestLookupValues;
  const HeadersWrapper<::Envoy::Http::RequestHeaderMap> headers_;
  const StreamInfo::StreamInfo& info_;
};

class ResponseWrapper : public BaseWrapper {
public:
  ResponseWrapper(Protobuf::Arena& arena, const ::Envoy::Http::ResponseHeaderMap* headers,
                  const ::Envoy::Http::ResponseTrailerMap* trailers,
                  const StreamInfo::StreamInfo& info)
      : BaseWrapper(arena), headers_(arena, headers), trailers_(arena, trailers), info_(info) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

protected:
  friend class ResponseLookupValues;
  const HeadersWrapper<::Envoy::Http::ResponseHeaderMap> headers_;
  const HeadersWrapper<::Envoy::Http::ResponseTrailerMap> trailers_;
  const StreamInfo::StreamInfo& info_;
};

class ConnectionWrapper : public BaseWrapper {
public:
  ConnectionWrapper(Protobuf::Arena& arena, const StreamInfo::StreamInfo& info)
      : BaseWrapper(arena), info_(info) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

protected:
  friend class ConnectionLookupValues;
  const StreamInfo::StreamInfo& info_;
};

class UpstreamWrapper : public BaseWrapper {
public:
  UpstreamWrapper(Protobuf::Arena& arena, const StreamInfo::StreamInfo& info)
      : BaseWrapper(arena), info_(info) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

protected:
  friend class UpstreamLookupValues;
  const StreamInfo::StreamInfo& info_;
};

class PeerWrapper : public BaseWrapper {
public:
  PeerWrapper(Protobuf::Arena& arena, const StreamInfo::StreamInfo& info, bool local)
      : BaseWrapper(arena), info_(info), local_(local) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

private:
  const StreamInfo::StreamInfo& info_;
  const bool local_;
};

class FilterStateWrapper : public BaseWrapper {
public:
  FilterStateWrapper(Protobuf::Arena& arena, const StreamInfo::FilterState& filter_state)
      : BaseWrapper(arena), filter_state_(filter_state) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

private:
  const StreamInfo::FilterState& filter_state_;
};

class XDSWrapper : public BaseWrapper {
public:
  XDSWrapper(Protobuf::Arena& arena, const StreamInfo::StreamInfo* info,
             const LocalInfo::LocalInfo* local_info)
      : BaseWrapper(arena), info_(info), local_info_(local_info) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

protected:
  friend class XDSLookupValues;
  const StreamInfo::StreamInfo* info_;
  const LocalInfo::LocalInfo* local_info_;
};

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
