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

absl::optional<CelValue> extractSslInfo(const Ssl::ConnectionInfo& ssl_info,
                                        absl::string_view value) {
  if (value == TLSVersion) {
    return CelValue::CreateString(&ssl_info.tlsVersion());
  } else if (value == SubjectLocalCertificate) {
    return CelValue::CreateString(&ssl_info.subjectLocalCertificate());
  } else if (value == SubjectPeerCertificate) {
    return CelValue::CreateString(&ssl_info.subjectPeerCertificate());
  } else if (value == URISanLocalCertificate) {
    if (!ssl_info.uriSanLocalCertificate().empty()) {
      return CelValue::CreateString(&ssl_info.uriSanLocalCertificate()[0]);
    }
  } else if (value == URISanPeerCertificate) {
    if (!ssl_info.uriSanPeerCertificate().empty()) {
      return CelValue::CreateString(&ssl_info.uriSanPeerCertificate()[0]);
    }
  } else if (value == DNSSanLocalCertificate) {
    if (!ssl_info.dnsSansLocalCertificate().empty()) {
      return CelValue::CreateString(&ssl_info.dnsSansLocalCertificate()[0]);
    }
  } else if (value == DNSSanPeerCertificate) {
    if (!ssl_info.dnsSansPeerCertificate().empty()) {
      return CelValue::CreateString(&ssl_info.dnsSansPeerCertificate()[0]);
    }
  } else if (value == SHA256PeerCertificateDigest) {
    if (!ssl_info.sha256PeerCertificateDigest().empty()) {
      return CelValue::CreateString(&ssl_info.sha256PeerCertificateDigest());
    }
  }
  return {};
}

} // namespace

absl::optional<CelValue> RequestWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();

  if (value == Headers) {
    return CelValue::CreateMap(&headers_);
  } else if (value == Time) {
    return CelValue::CreateTimestamp(absl::FromChrono(info_.startTime()));
  } else if (value == Size) {
    // it is important to make a choice whether to rely on content-length vs stream info
    // (which is not available at the time of the request headers)
    if (headers_.value_ != nullptr && headers_.value_->ContentLength() != nullptr) {
      int64_t length;
      if (absl::SimpleAtoi(headers_.value_->getContentLengthValue(), &length)) {
        return CelValue::CreateInt64(length);
      }
    } else {
      return CelValue::CreateInt64(info_.bytesReceived());
    }
  } else if (value == TotalSize) {
    return CelValue::CreateInt64(info_.bytesReceived() +
                                 (headers_.value_ ? headers_.value_->byteSize() : 0));
  } else if (value == Duration) {
    auto duration = info_.requestComplete();
    if (duration.has_value()) {
      return CelValue::CreateDuration(absl::FromChrono(duration.value()));
    }
  } else if (value == Protocol) {
    if (info_.protocol().has_value()) {
      return CelValue::CreateString(&Http::Utility::getProtocolString(info_.protocol().value()));
    } else {
      return {};
    }
  }

  if (headers_.value_ != nullptr) {
    if (value == Path) {
      return convertHeaderEntry(headers_.value_->Path());
    } else if (value == UrlPath) {
      absl::string_view path = headers_.value_->getPathValue();
      size_t query_offset = path.find('?');
      if (query_offset == absl::string_view::npos) {
        return CelValue::CreateStringView(path);
      }
      return CelValue::CreateStringView(path.substr(0, query_offset));
    } else if (value == Host) {
      return convertHeaderEntry(headers_.value_->Host());
    } else if (value == Scheme) {
      return convertHeaderEntry(headers_.value_->Scheme());
    } else if (value == Method) {
      return convertHeaderEntry(headers_.value_->Method());
    } else if (value == Referer) {
      return convertHeaderEntry(headers_.value_->getInline(referer_handle.handle()));
    } else if (value == ID) {
      return convertHeaderEntry(headers_.value_->RequestId());
    } else if (value == UserAgent) {
      return convertHeaderEntry(headers_.value_->UserAgent());
    } else if (value == Query) {
      absl::string_view path = headers_.value_->getPathValue();
      auto query_offset = path.find('?');
      if (query_offset == absl::string_view::npos) {
        return CelValue::CreateStringView(absl::string_view());
      }
      path = path.substr(query_offset + 1);
      auto fragment_offset = path.find('#');
      return CelValue::CreateStringView(path.substr(0, fragment_offset));
    }
  }
  return {};
}

absl::optional<CelValue> ResponseWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == Code) {
    auto code = info_.responseCode();
    if (code.has_value()) {
      return CelValue::CreateInt64(code.value());
    }
    return {};
  } else if (value == Size) {
    return CelValue::CreateInt64(info_.bytesSent());
  } else if (value == Headers) {
    return CelValue::CreateMap(&headers_);
  } else if (value == Trailers) {
    return CelValue::CreateMap(&trailers_);
  } else if (value == Flags) {
    return CelValue::CreateInt64(info_.responseFlags());
  } else if (value == GrpcStatus) {
    auto const& optional_status = Grpc::Common::getGrpcStatus(
        trailers_.value_ ? *trailers_.value_ : *Http::StaticEmptyHeaders::get().response_trailers,
        headers_.value_ ? *headers_.value_ : *Http::StaticEmptyHeaders::get().response_headers,
        info_);
    if (optional_status.has_value()) {
      return CelValue::CreateInt64(optional_status.value());
    }
    return {};
  } else if (value == TotalSize) {
    return CelValue::CreateInt64(info_.bytesSent() +
                                 (headers_.value_ ? headers_.value_->byteSize() : 0) +
                                 (trailers_.value_ ? trailers_.value_->byteSize() : 0));
  } else if (value == CodeDetails) {
    const absl::optional<std::string>& details = info_.responseCodeDetails();
    if (details.has_value()) {
      return CelValue::CreateString(&details.value());
    }
    return {};
  }
  return {};
}

absl::optional<CelValue> ConnectionWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == MTLS) {
    return CelValue::CreateBool(
        info_.downstreamAddressProvider().sslConnection() != nullptr &&
        info_.downstreamAddressProvider().sslConnection()->peerCertificatePresented());
  } else if (value == RequestedServerName) {
    return CelValue::CreateStringView(info_.downstreamAddressProvider().requestedServerName());
  } else if (value == ID) {
    auto id = info_.downstreamAddressProvider().connectionID();
    if (id.has_value()) {
      return CelValue::CreateUint64(id.value());
    }
    return {};
  } else if (value == ConnectionTerminationDetails) {
    if (info_.connectionTerminationDetails().has_value()) {
      return CelValue::CreateString(&info_.connectionTerminationDetails().value());
    }
    return {};
  } else if (value == DownstreamTransportFailureReason) {
    if (!info_.downstreamTransportFailureReason().empty()) {
      return CelValue::CreateStringView(info_.downstreamTransportFailureReason());
    }
    return {};
  }

  auto ssl_info = info_.downstreamAddressProvider().sslConnection();
  if (ssl_info != nullptr) {
    return extractSslInfo(*ssl_info, value);
  }

  return {};
}

absl::optional<CelValue> UpstreamWrapper::operator[](CelValue key) const {
  if (!key.IsString() || !info_.upstreamInfo().has_value()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == Address) {
    auto upstream_host = info_.upstreamInfo().value().get().upstreamHost();
    if (upstream_host != nullptr && upstream_host->address() != nullptr) {
      return CelValue::CreateStringView(upstream_host->address()->asStringView());
    }
  } else if (value == Port) {
    auto upstream_host = info_.upstreamInfo().value().get().upstreamHost();
    if (upstream_host != nullptr && upstream_host->address() != nullptr &&
        upstream_host->address()->ip() != nullptr) {
      return CelValue::CreateInt64(upstream_host->address()->ip()->port());
    }
  } else if (value == UpstreamLocalAddress) {
    auto upstream_local_address = info_.upstreamInfo().value().get().upstreamLocalAddress();
    if (upstream_local_address != nullptr) {
      return CelValue::CreateStringView(upstream_local_address->asStringView());
    }
  } else if (value == UpstreamTransportFailureReason) {
    return CelValue::CreateStringView(
        info_.upstreamInfo().value().get().upstreamTransportFailureReason());
  }

  auto ssl_info = info_.upstreamInfo().value().get().upstreamSslConnection();
  if (ssl_info != nullptr) {
    return extractSslInfo(*ssl_info, value);
  }

  return {};
}

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

class FilterStateObjectWrapper : public google::api::expr::runtime::CelMap {
public:
  FilterStateObjectWrapper(const StreamInfo::FilterState::ObjectReflection* reflection)
      : reflection_(reflection) {}
  absl::optional<CelValue> operator[](CelValue key) const override {
    if (reflection_ == nullptr || !key.IsString()) {
      return {};
    }
    auto field_value = reflection_->getField(key.StringOrDie().value());
    return absl::visit(Visitor{}, field_value);
  }
  // Default stubs.
  int size() const override { return 0; }
  bool empty() const override { return true; }
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
  const StreamInfo::FilterState::ObjectReflection* reflection_;
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
    } else if (object != nullptr) {
      // Attempt to find the reflection object.
      auto factory =
          Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(value);
      if (factory) {
        auto reflection = factory->reflect(object);
        if (reflection) {
          auto* raw_reflection = reflection.release();
          arena_.Own(raw_reflection);
          return CelValue::CreateMap(
              ProtobufWkt::Arena::Create<FilterStateObjectWrapper>(&arena_, raw_reflection));
        }
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

absl::optional<CelValue> XDSWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == ClusterName) {
    const auto cluster_info = info_.upstreamClusterInfo();
    if (cluster_info && cluster_info.value()) {
      return CelValue::CreateString(&cluster_info.value()->name());
    }
  } else if (value == ClusterMetadata) {
    const auto cluster_info = info_.upstreamClusterInfo();
    if (cluster_info && cluster_info.value()) {
      return CelProtoWrapper::CreateMessage(&cluster_info.value()->metadata(), &arena_);
    }
  } else if (value == RouteName) {
    if (info_.route()) {
      return CelValue::CreateString(&info_.route()->routeName());
    }
  } else if (value == RouteMetadata) {
    if (info_.route()) {
      return CelProtoWrapper::CreateMessage(&info_.route()->metadata(), &arena_);
    }
  } else if (value == UpstreamHostMetadata) {
    const auto upstream_info = info_.upstreamInfo();
    if (upstream_info && upstream_info->upstreamHost()) {
      return CelProtoWrapper::CreateMessage(upstream_info->upstreamHost()->metadata().get(),
                                            &arena_);
    }
  } else if (value == FilterChainName) {
    const auto filter_chain_info = info_.downstreamAddressProvider().filterChainInfo();
    const absl::string_view filter_chain_name =
        filter_chain_info.has_value() ? filter_chain_info->name() : absl::string_view{};
    return CelValue::CreateStringView(filter_chain_name);
  }
  return {};
}

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
