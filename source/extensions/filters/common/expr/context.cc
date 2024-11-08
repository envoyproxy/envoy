#include "source/extensions/filters/common/expr/context.h"

#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/common/expr/cel_state.h"

#include "absl/strings/numbers.h"
#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    referer_handle(Http::CustomHeaders::get().Referer);

absl::optional<CelValue> convertHeaderEntry(const Http::HeaderEntry* header) {
  if (header == nullptr) {
    return {};
  }
  return CelValue::CreateStringView(header->value().getStringView());
}

absl::optional<CelValue>
convertHeaderEntry(Protobuf::Arena& arena,
                   Http::HeaderUtility::GetAllOfHeaderAsStringResult&& result) {
  if (!result.result().has_value()) {
    return {};
  } else if (!result.backingString().empty()) {
    return CelValue::CreateString(
        Protobuf::Arena::Create<std::string>(&arena, result.backingString()));
  } else {
    return CelValue::CreateStringView(result.result().value());
  }
}

namespace {

const absl::flat_hash_map<absl::string_view, SslExtractor>& getSslExtractors() {
  static const auto* extractors = new absl::flat_hash_map<absl::string_view, SslExtractor>{
      {TLSVersion,
       [](const Ssl::ConnectionInfo& info) { return CelValue::CreateString(&info.tlsVersion()); }},
      {SubjectLocalCertificate,
       [](const Ssl::ConnectionInfo& info) {
         return CelValue::CreateString(&info.subjectLocalCertificate());
       }},
      {SubjectPeerCertificate,
       [](const Ssl::ConnectionInfo& info) {
         return CelValue::CreateString(&info.subjectPeerCertificate());
       }},
      {URISanLocalCertificate,
       [](const Ssl::ConnectionInfo& info) {
         return !info.uriSanLocalCertificate().empty()
                    ? CelValue::CreateString(&info.uriSanLocalCertificate()[0])
                    : CelValue::CreateNull();
       }},
      {URISanPeerCertificate,
       [](const Ssl::ConnectionInfo& info) {
         return !info.uriSanPeerCertificate().empty()
                    ? CelValue::CreateString(&info.uriSanPeerCertificate()[0])
                    : CelValue::CreateNull();
       }},
      {DNSSanLocalCertificate,
       [](const Ssl::ConnectionInfo& info) {
         return !info.dnsSansLocalCertificate().empty()
                    ? CelValue::CreateString(&info.dnsSansLocalCertificate()[0])
                    : CelValue::CreateNull();
       }},
      {DNSSanPeerCertificate,
       [](const Ssl::ConnectionInfo& info) {
         return !info.dnsSansPeerCertificate().empty()
                    ? CelValue::CreateString(&info.dnsSansPeerCertificate()[0])
                    : CelValue::CreateNull();
       }},
      {SHA256PeerCertificateDigest, [](const Ssl::ConnectionInfo& info) {
         return !info.sha256PeerCertificateDigest().empty()
                    ? CelValue::CreateString(&info.sha256PeerCertificateDigest())
                    : CelValue::CreateNull();
       }}};
  return *extractors;
}

absl::optional<CelValue> extractSslInfo(const Ssl::ConnectionInfo& ssl_info,
                                        absl::string_view value) {
  const auto& extractors = getSslExtractors();
  auto it = extractors.find(value);
  if (it != extractors.end()) {
    return it->second(ssl_info);
  }
  return {};
}

} // namespace

// RequestLookup implementation
const absl::flat_hash_map<absl::string_view, CelValueExtractor>& RequestLookup::get() {
  static const auto* const instance =
      new absl::flat_hash_map<absl::string_view, CelValueExtractor>(create());
  return *instance;
}

absl::flat_hash_map<absl::string_view, CelValueExtractor> RequestLookup::create() {
  return {{Headers,
           [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             return CelValue::CreateMap(&wrapper.headers_);
           }},
          {Time,
           [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             return CelValue::CreateTimestamp(absl::FromChrono(wrapper.info_.startTime()));
           }},
          {Size,
           [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             if (wrapper.headers_.value_ != nullptr &&
                 wrapper.headers_.value_->ContentLength() != nullptr) {
               int64_t length;
               if (absl::SimpleAtoi(wrapper.headers_.value_->getContentLengthValue(), &length)) {
                 return CelValue::CreateInt64(length);
               }
             }
             return CelValue::CreateInt64(wrapper.info_.bytesReceived());
           }},
          {TotalSize,
           [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             return CelValue::CreateInt64(
                 wrapper.info_.bytesReceived() +
                 (wrapper.headers_.value_ ? wrapper.headers_.value_->byteSize() : 0));
           }},
          {Duration,
           [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             auto duration = wrapper.info_.requestComplete();
             if (duration.has_value()) {
               return CelValue::CreateDuration(absl::FromChrono(duration.value()));
             }
             return {};
           }},
          {Protocol,
           [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             if (wrapper.info_.protocol().has_value()) {
               return CelValue::CreateString(
                   &Http::Utility::getProtocolString(wrapper.info_.protocol().value()));
             }
             return {};
           }},
          {Path,
           [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             return wrapper.headers_.value_ ? convertHeaderEntry(wrapper.headers_.value_->Path())
                                            : absl::optional<CelValue>{};
           }},
          {UrlPath,
           [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             if (!wrapper.headers_.value_) {
               return {};
             }
             absl::string_view path = wrapper.headers_.value_->getPathValue();
             size_t query_offset = path.find('?');
             if (query_offset == absl::string_view::npos) {
               return CelValue::CreateStringView(path);
             }
             return CelValue::CreateStringView(path.substr(0, query_offset));
           }},
          {Host,
           [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             return wrapper.headers_.value_ ? convertHeaderEntry(wrapper.headers_.value_->Host())
                                            : absl::optional<CelValue>{};
           }},
          {Scheme,
           [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             return wrapper.headers_.value_ ? convertHeaderEntry(wrapper.headers_.value_->Scheme())
                                            : absl::optional<CelValue>{};
           }},
          {Method,
           [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             return wrapper.headers_.value_ ? convertHeaderEntry(wrapper.headers_.value_->Method())
                                            : absl::optional<CelValue>{};
           }},
          {Referer,
           [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             return wrapper.headers_.value_ ? convertHeaderEntry(wrapper.headers_.value_->getInline(
                                                  referer_handle.handle()))
                                            : absl::optional<CelValue>{};
           }},
          {ID,
           [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             return wrapper.headers_.value_
                        ? convertHeaderEntry(wrapper.headers_.value_->RequestId())
                        : absl::optional<CelValue>{};
           }},
          {UserAgent,
           [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             return wrapper.headers_.value_
                        ? convertHeaderEntry(wrapper.headers_.value_->UserAgent())
                        : absl::optional<CelValue>{};
           }},
          {Query, [](const RequestWrapper& wrapper) -> absl::optional<CelValue> {
             if (!wrapper.headers_.value_) {
               return {};
             }
             absl::string_view path = wrapper.headers_.value_->getPathValue();
             auto query_offset = path.find('?');
             if (query_offset == absl::string_view::npos) {
               return CelValue::CreateStringView(absl::string_view());
             }
             path = path.substr(query_offset + 1);
             auto fragment_offset = path.find('#');
             return CelValue::CreateStringView(path.substr(0, fragment_offset));
           }}};
}

// ResponseLookup implementation
const absl::flat_hash_map<absl::string_view, ResponseValueExtractor>& ResponseLookup::get() {
  static const auto* const instance =
      new absl::flat_hash_map<absl::string_view, ResponseValueExtractor>(create());
  return *instance;
}

absl::flat_hash_map<absl::string_view, ResponseValueExtractor> ResponseLookup::create() {
  return {{Code,
           [](const ResponseWrapper& wrapper) -> absl::optional<CelValue> {
             auto code = wrapper.info_.responseCode();
             return code.has_value() ? CelValue::CreateInt64(code.value())
                                     : absl::optional<CelValue>{};
           }},
          {Size,
           [](const ResponseWrapper& wrapper) -> absl::optional<CelValue> {
             return CelValue::CreateInt64(wrapper.info_.bytesSent());
           }},
          {Headers,
           [](const ResponseWrapper& wrapper) -> absl::optional<CelValue> {
             return CelValue::CreateMap(&wrapper.headers_);
           }},
          {Trailers,
           [](const ResponseWrapper& wrapper) -> absl::optional<CelValue> {
             return CelValue::CreateMap(&wrapper.trailers_);
           }},
          {Flags,
           [](const ResponseWrapper& wrapper) -> absl::optional<CelValue> {
             return CelValue::CreateInt64(wrapper.info_.legacyResponseFlags());
           }},
          {GrpcStatus,
           [](const ResponseWrapper& wrapper) -> absl::optional<CelValue> {
             auto const& optional_status = Grpc::Common::getGrpcStatus(
                 wrapper.trailers_.value_ ? *wrapper.trailers_.value_
                                          : *Http::StaticEmptyHeaders::get().response_trailers,
                 wrapper.headers_.value_ ? *wrapper.headers_.value_
                                         : *Http::StaticEmptyHeaders::get().response_headers,
                 wrapper.info_);
             return optional_status.has_value() ? CelValue::CreateInt64(optional_status.value())
                                                : absl::optional<CelValue>{};
           }},
          {TotalSize,
           [](const ResponseWrapper& wrapper) -> absl::optional<CelValue> {
             return CelValue::CreateInt64(
                 wrapper.info_.bytesSent() +
                 (wrapper.headers_.value_ ? wrapper.headers_.value_->byteSize() : 0) +
                 (wrapper.trailers_.value_ ? wrapper.trailers_.value_->byteSize() : 0));
           }},
          {CodeDetails,
           [](const ResponseWrapper& wrapper) -> absl::optional<CelValue> {
             const absl::optional<std::string>& details = wrapper.info_.responseCodeDetails();
             return details.has_value() ? CelValue::CreateString(&details.value())
                                        : absl::optional<CelValue>{};
           }},
          {BackendLatency, [](const ResponseWrapper& wrapper) -> absl::optional<CelValue> {
             Envoy::StreamInfo::TimingUtility timing(wrapper.info_);
             const auto last_upstream_rx_byte_received = timing.lastUpstreamRxByteReceived();
             const auto first_upstream_tx_byte_sent = timing.firstUpstreamTxByteSent();
             if (last_upstream_rx_byte_received.has_value() &&
                 first_upstream_tx_byte_sent.has_value()) {
               return CelValue::CreateDuration(absl::FromChrono(
                   last_upstream_rx_byte_received.value() - first_upstream_tx_byte_sent.value()));
             }
             return {};
           }}};
}

// ConnectionLookup implementation
const absl::flat_hash_map<absl::string_view, ConnectionValueExtractor>& ConnectionLookup::get() {
  static const auto* const instance =
      new absl::flat_hash_map<absl::string_view, ConnectionValueExtractor>(create());
  return *instance;
}

absl::flat_hash_map<absl::string_view, ConnectionValueExtractor> ConnectionLookup::create() {
  return {
      {MTLS,
       [](const ConnectionWrapper& wrapper) -> absl::optional<CelValue> {
         return CelValue::CreateBool(
             wrapper.info_.downstreamAddressProvider().sslConnection() != nullptr &&
             wrapper.info_.downstreamAddressProvider().sslConnection()->peerCertificatePresented());
       }},
      {RequestedServerName,
       [](const ConnectionWrapper& wrapper) -> absl::optional<CelValue> {
         return CelValue::CreateStringView(
             wrapper.info_.downstreamAddressProvider().requestedServerName());
       }},
      {ID,
       [](const ConnectionWrapper& wrapper) -> absl::optional<CelValue> {
         auto id = wrapper.info_.downstreamAddressProvider().connectionID();
         return id.has_value() ? CelValue::CreateUint64(id.value()) : absl::optional<CelValue>{};
       }},
      {ConnectionTerminationDetails,
       [](const ConnectionWrapper& wrapper) -> absl::optional<CelValue> {
         if (wrapper.info_.connectionTerminationDetails().has_value()) {
           return CelValue::CreateString(&wrapper.info_.connectionTerminationDetails().value());
         }
         return {};
       }},
      {DownstreamTransportFailureReason,
       [](const ConnectionWrapper& wrapper) -> absl::optional<CelValue> {
         if (!wrapper.info_.downstreamTransportFailureReason().empty()) {
           return CelValue::CreateStringView(wrapper.info_.downstreamTransportFailureReason());
         }
         return {};
       }},
  };
}

// UpstreamLookup implementation
const absl::flat_hash_map<absl::string_view, UpstreamValueExtractor>& UpstreamLookup::get() {
  static const auto* const instance =
      new absl::flat_hash_map<absl::string_view, UpstreamValueExtractor>(create());
  return *instance;
}

absl::flat_hash_map<absl::string_view, UpstreamValueExtractor> UpstreamLookup::create() {
  return {
      {Address,
       [](const UpstreamWrapper& wrapper) -> absl::optional<CelValue> {
         if (!wrapper.info_.upstreamInfo().has_value()) {
           return {};
         }
         auto upstream_host = wrapper.info_.upstreamInfo().value().get().upstreamHost();
         if (upstream_host != nullptr && upstream_host->address() != nullptr) {
           return CelValue::CreateStringView(upstream_host->address()->asStringView());
         }
         return {};
       }},
      {Port,
       [](const UpstreamWrapper& wrapper) -> absl::optional<CelValue> {
         if (!wrapper.info_.upstreamInfo().has_value()) {
           return {};
         }
         auto upstream_host = wrapper.info_.upstreamInfo().value().get().upstreamHost();
         if (upstream_host != nullptr && upstream_host->address() != nullptr &&
             upstream_host->address()->ip() != nullptr) {
           return CelValue::CreateInt64(upstream_host->address()->ip()->port());
         }
         return {};
       }},
      {UpstreamLocalAddress,
       [](const UpstreamWrapper& wrapper) -> absl::optional<CelValue> {
         if (!wrapper.info_.upstreamInfo().has_value()) {
           return {};
         }
         auto upstream_local_address =
             wrapper.info_.upstreamInfo().value().get().upstreamLocalAddress();
         if (upstream_local_address != nullptr) {
           return CelValue::CreateStringView(upstream_local_address->asStringView());
         }
         return {};
       }},
      {UpstreamTransportFailureReason,
       [](const UpstreamWrapper& wrapper) -> absl::optional<CelValue> {
         if (!wrapper.info_.upstreamInfo().has_value()) {
           return {};
         }
         return CelValue::CreateStringView(
             wrapper.info_.upstreamInfo().value().get().upstreamTransportFailureReason());
       }},
      {UpstreamRequestAttemptCount, [](const UpstreamWrapper& wrapper) -> absl::optional<CelValue> {
         return CelValue::CreateUint64(wrapper.info_.attemptCount().value_or(0));
       }}};
}

// RequestWrapper implementation
absl::optional<CelValue> RequestWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();

  const auto& lookup = RequestLookup::get();
  auto it = lookup.find(value);
  if (it != lookup.end()) {
    return it->second(*this);
  }
  return {};
}

// ResponseWrapper implementation
absl::optional<CelValue> ResponseWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();

  const auto& lookup = ResponseLookup::get();
  auto it = lookup.find(value);
  if (it != lookup.end()) {
    return it->second(*this);
  }
  return {};
}

// ConnectionWrapper implementation
absl::optional<CelValue> ConnectionWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();

  const auto& lookup = ConnectionLookup::get();
  auto it = lookup.find(value);
  if (it != lookup.end()) {
    return it->second(*this);
  }

  // Handle SSL info separately
  auto ssl_info = info_.downstreamAddressProvider().sslConnection();
  if (ssl_info != nullptr) {
    return extractSslInfo(*ssl_info, value);
  }
  return {};
}

// UpstreamWrapper implementation
absl::optional<CelValue> UpstreamWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();

  const auto& lookup = UpstreamLookup::get();
  auto it = lookup.find(value);
  if (it != lookup.end()) {
    return it->second(*this);
  }

  // Handle SSL info if available
  if (info_.upstreamInfo().has_value()) {
    auto ssl_info = info_.upstreamInfo().value().get().upstreamSslConnection();
    if (ssl_info != nullptr) {
      return extractSslInfo(*ssl_info, value);
    }
  }
  return {};
}

// PeerWrapper implementation
absl::optional<CelValue> PeerWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();

  if (value == Address) {
    if (local_) {
      return CelValue::CreateStringView(
          info_.downstreamAddressProvider().localAddress()->asStringView());
    } else {
      return CelValue::CreateStringView(
          info_.downstreamAddressProvider().remoteAddress()->asStringView());
    }
  } else if (value == Port) {
    if (local_) {
      if (info_.downstreamAddressProvider().localAddress()->ip() != nullptr) {
        return CelValue::CreateInt64(
            info_.downstreamAddressProvider().localAddress()->ip()->port());
      }
    } else {
      if (info_.downstreamAddressProvider().remoteAddress()->ip() != nullptr) {
        return CelValue::CreateInt64(
            info_.downstreamAddressProvider().remoteAddress()->ip()->port());
      }
    }
  }

  return {};
}

// FilterStateWrapper implementation
class FilterStateObjectWrapper : public google::api::expr::runtime::CelMap {
public:
  FilterStateObjectWrapper(const StreamInfo::FilterState::Object* object) : object_(object) {}

  absl::optional<CelValue> operator[](CelValue key) const override {
    if (object_ == nullptr || !key.IsString()) {
      return {};
    }
    auto field_value = object_->getField(key.StringOrDie().value());
    return absl::visit(Visitor{}, field_value);
  }

  int size() const override { return 0; }
  bool empty() const override { return true; }
  using CelMap::ListKeys;
  absl::StatusOr<const google::api::expr::runtime::CelList*> ListKeys() const override {
    return &WrapperFields::get().Empty;
  }

private:
  struct Visitor {
    absl::optional<CelValue> operator()(int64_t val) { return CelValue::CreateInt64(val); }
    absl::optional<CelValue> operator()(absl::string_view val) {
      return CelValue::CreateStringView(val);
    }
    absl::optional<CelValue> operator()(absl::monostate) { return {}; }
  };
  const StreamInfo::FilterState::Object* object_;
};

absl::optional<CelValue> FilterStateWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();

  if (const StreamInfo::FilterState::Object* object = filter_state_.getDataReadOnlyGeneric(value);
      object != nullptr) {
    const CelState* cel_state = dynamic_cast<const CelState*>(object);
    if (cel_state) {
      return cel_state->exprValue(&arena_, false);
    } else {
      if (object->hasFieldSupport()) {
        return CelValue::CreateMap(
            ProtobufWkt::Arena::Create<FilterStateObjectWrapper>(&arena_, object));
      }
      absl::optional<std::string> serialized = object->serializeAsString();
      if (serialized.has_value()) {
        std::string* out = ProtobufWkt::Arena::Create<std::string>(&arena_, serialized.value());
        return CelValue::CreateBytes(out);
      }
    }
  }
  return {};
}

// XDSLookup implementation
const absl::flat_hash_map<absl::string_view, XDSValueExtractor>& XDSLookup::get() {
  static const auto* const instance =
      new absl::flat_hash_map<absl::string_view, XDSValueExtractor>(create());
  return *instance;
}

absl::flat_hash_map<absl::string_view, XDSValueExtractor> XDSLookup::create() {
  return {{Node,
           [](const XDSWrapper& wrapper) -> absl::optional<CelValue> {
             if (wrapper.local_info_) {
               return CelProtoWrapper::CreateMessage(&wrapper.local_info_->node(), &wrapper.arena_);
             }
             return {};
           }},
          {ClusterName,
           [](const XDSWrapper& wrapper) -> absl::optional<CelValue> {
             if (wrapper.info_ == nullptr) {
               return {};
             }
             const auto cluster_info = wrapper.info_->upstreamClusterInfo();
             if (cluster_info && cluster_info.value()) {
               return CelValue::CreateString(&cluster_info.value()->name());
             }
             return {};
           }},
          {ClusterMetadata,
           [](const XDSWrapper& wrapper) -> absl::optional<CelValue> {
             if (wrapper.info_ == nullptr) {
               return {};
             }
             const auto cluster_info = wrapper.info_->upstreamClusterInfo();
             if (cluster_info && cluster_info.value()) {
               return CelProtoWrapper::CreateMessage(&cluster_info.value()->metadata(),
                                                     &wrapper.arena_);
             }
             return {};
           }},
          {RouteName,
           [](const XDSWrapper& wrapper) -> absl::optional<CelValue> {
             if (wrapper.info_ == nullptr || !wrapper.info_->route()) {
               return {};
             }
             return CelValue::CreateString(&wrapper.info_->route()->routeName());
           }},
          {RouteMetadata,
           [](const XDSWrapper& wrapper) -> absl::optional<CelValue> {
             if (wrapper.info_ == nullptr || !wrapper.info_->route()) {
               return {};
             }
             return CelProtoWrapper::CreateMessage(&wrapper.info_->route()->metadata(),
                                                   &wrapper.arena_);
           }},
          {UpstreamHostMetadata,
           [](const XDSWrapper& wrapper) -> absl::optional<CelValue> {
             if (wrapper.info_ == nullptr) {
               return {};
             }
             const auto upstream_info = wrapper.info_->upstreamInfo();
             if (upstream_info && upstream_info->upstreamHost()) {
               return CelProtoWrapper::CreateMessage(
                   upstream_info->upstreamHost()->metadata().get(), &wrapper.arena_);
             }
             return {};
           }},
          {FilterChainName,
           [](const XDSWrapper& wrapper) -> absl::optional<CelValue> {
             if (wrapper.info_ == nullptr) {
               return {};
             }
             const auto filter_chain_info =
                 wrapper.info_->downstreamAddressProvider().filterChainInfo();
             const absl::string_view filter_chain_name =
                 filter_chain_info.has_value() ? filter_chain_info->name() : absl::string_view{};
             return CelValue::CreateStringView(filter_chain_name);
           }},
          {ListenerMetadata,
           [](const XDSWrapper& wrapper) -> absl::optional<CelValue> {
             if (wrapper.info_ == nullptr) {
               return {};
             }
             const auto listener_info = wrapper.info_->downstreamAddressProvider().listenerInfo();
             if (listener_info) {
               return CelProtoWrapper::CreateMessage(&listener_info->metadata(), &wrapper.arena_);
             }
             return {};
           }},
          {ListenerDirection, [](const XDSWrapper& wrapper) -> absl::optional<CelValue> {
             if (wrapper.info_ == nullptr) {
               return {};
             }
             const auto listener_info = wrapper.info_->downstreamAddressProvider().listenerInfo();
             if (listener_info) {
               return CelValue::CreateInt64(listener_info->direction());
             }
             return {};
           }}};
}

absl::optional<CelValue> XDSWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();

  const auto& lookup = XDSLookup::get();
  auto it = lookup.find(value);
  if (it != lookup.end()) {
    return it->second(*this);
  }
  return {};
}

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
