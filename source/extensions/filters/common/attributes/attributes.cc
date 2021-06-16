#include <atomic>
#include <chrono>
#include <iterator>
#include <map>
#include <memory>
#include <string>

#include "absl/strings/str_format.h"

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/sink.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/network/filter.h"

#include "source/common/grpc/common.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/utility.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/common/fmt.h"
#include "source/common/common/lock_guard.h"
#include "source/common/http/header_map_impl.h"

#include "source/extensions/filters/common/attributes/attributes.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Attributes {

using google::api::expr::v1alpha1::MapValue;
using google::api::expr::v1alpha1::MapValue_Entry;
using google::api::expr::v1alpha1::Value;
using google::protobuf::NullValue;
using HashPolicy = envoy::config::route::v3::RouteAction::HashPolicy;
using AttrMap = ProtobufWkt::Map<std::string, ProtobufWkt::Value>;

Value ExprValueUtil::mapValue(MapValue* m) {
  Value val;
  val.set_allocated_map_value(m);
  return val;
}

Value ExprValueUtil::stringValue(const std::string& str) {
  Value val;
  val.set_string_value(str);
  return val;
}

Value ExprValueUtil::optionalStringValue(const absl::optional<std::string>& str) {
  if (str.has_value()) {
    return ExprValueUtil::stringValue(str.value());
  }
  return ExprValueUtil::nullValue();
}

Value ExprValueUtil::uint64Value(uint64_t n) {
  Value val;
  val.set_uint64_value(n);
  return val;
}

const Value ExprValueUtil::nullValue() {
  Value v1;
  Value v;
  v.set_null_value(v1.null_value());
  return v;
}

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    referer_handle = Http::CustomHeaders::get().Referer;

ProtobufWkt::Map<std::string, ProtobufWkt::Struct>& AttrState::build() {
  for (const std::string& s : specified_) {
    auto [root_tok, sub_tok] = tokenizePath(absl::string_view(s));
    findValue(root_tok, sub_tok);
  }
  return attributes_;
}

std::vector<std::tuple<absl::string_view, absl::string_view>>
AttrUtils::tokenizeAttrs(const google::protobuf::RepeatedPtrField<std::string> attrs) {
  std::vector<std::tuple<absl::string_view, absl::string_view>> v;
  for (const std::string& s : attrs) {
    v.push_back(AttrUtils::tokenizeAttrPath(absl::string_view(s)));
  }
  return v;
}
static std::tuple<absl::string_view, absl::string_view>
AttrUtils::tokenizeAttrPath(absl::string_view path) {
  // ex: "request.foobar"
  //             ^
  size_t root_token_str_end = std::min(path.find('.'), path.find('\0'));
  if (root_token_str_end == absl::string_view::npos) {
    root_token_str_end = path.size();
  }

  auto root_tok = path.substr(0, root_token_str_end);
  absl::string_view sub_tok;
  if (root_token_str_end + 1 < path.size()) {
    sub_tok = path.substr(root_token_str_end + 1,
                          std::min(path.find('\0'), path.size() - root_token_str_end - 1));
  }
  return std::tuple(root_tok, sub_tok);
}

absl::optional<Value> AttrState::findValue(absl::string_view root_tok, absl::string_view sub_tok) {
  auto root_id = property_tokens.find(root_tok);
  if (root_id == property_tokens.end()) {
    ENVOY_LOG(debug, "The attribute '{}.{}' is not a valid ext_proc attribute", root_tok, sub_tok);
    return absl::nullopt;
  }

  switch (root_id->second) {
  case PropertyToken::REQUEST:
    return AttrState::requestSet(sub_tok);
  case PropertyToken::RESPONSE:
    return AttrState::responseSet(sub_tok);
  case PropertyToken::CONNECTION:
    return AttrState::connectionSet(sub_tok);
  case PropertyToken::UPSTREAM:
    return AttrState::upstreamSet(sub_tok);
  case PropertyToken::SOURCE:
    return AttrState::sourceSet(sub_tok);
  case PropertyToken::DESTINATION:
    return AttrState::destinationSet(sub_tok);
  case PropertyToken::METADATA:
    if (sub_tok.empty()) {
      return AttrState::metadataSet();
    }
    return ExprValueUtil::nullValue();
  case PropertyToken::FILTER_STATE:
    if (sub_tok.empty()) {
      return AttrState::filterStateSet();
    }
    return ExprValueUtil::nullValue();
  }
}

absl::optional<Value> AttrState::requestSet(absl::string_view path) {
  auto attr_fields = getOrInsert(REQUEST_TOKEN);

  auto part_token = request_tokens.find(path);
  if (part_token == request_tokens.end()) {
    ENVOY_LOG(debug, "Unable to find ext_proc request attribute: '{}'", path);
    return absl::nullopt;
  }

  auto headers = request_headers_;
  int end;

  switch (part_token->second) {
  case RequestToken::PATH:
    if (headers != nullptr) {
      return ExprValueUtil::stringValue(std::string(headers->getPathValue()));
    }
    break;
  case RequestToken::URL_PATH:
    if (headers != nullptr && headers->Path() != nullptr && headers->Path()->value() != nullptr) {
      end = std::max(path.find('\0'), path.find('?'));
      return ExprValueUtil::stringValue(
          std::string(headers->Path()->value().getStringView().substr(0, end)));
    }
    break;
  case RequestToken::HOST:
    if (headers != nullptr) {
      return ExprValueUtil::stringValue(std::string(headers->getHostValue()));
    }
    break;
  case RequestToken::SCHEME:
    if (headers != nullptr) {
      return ExprValueUtil::stringValue(std::string(headers->getSchemeValue()));
    }
    break;
  case RequestToken::METHOD:
    if (headers != nullptr) {
      return ExprValueUtil::stringValue(std::string(headers->getMethodValue()));
    }
    break;
  case RequestToken::HEADERS:
    ENVOY_LOG(debug, "ignoring unimplemented attribute request.headers");
    break;
  case RequestToken::REFERER:
    if (headers != nullptr) {
      return ExprValueUtil::stringValue(
          std::string(headers->getInlineValue(referer_handle.handle())));
    }
    break;
  case RequestToken::USERAGENT:
    if (headers != nullptr) {
      return ExprValueUtil::stringValue(std::string(headers->getUserAgentValue()));
    }
    break;
  case RequestToken::TIME:
    return ExprValueUtil::stringValue(getTs());
    break;
  case RequestToken::ID:
    if (headers != nullptr) {
      return ExprValueUtil::stringValue(std::string(headers->getRequestIdValue()));
    }
    break;
  case RequestToken::PROTOCOL:
    return ExprValueUtil::optionalStringValue(
        HttpProtocolStrings[static_cast<int>(stream_info_.protocol().value())]);
  case RequestToken::DURATION:
    if (stream_info_.requestComplete().has_value()) {
      return ExprValueUtil::stringValue(
          formatDuration(absl::FromChrono(stream_info_.requestComplete().value())));
    }
    break;
  case RequestToken::SIZE:
    if (headers != nullptr && headers->ContentLength() != nullptr) {
      int64_t length;
      if (absl::SimpleAtoi(headers->ContentLength()->value().getStringView(), &length)) {
        return ExprValueUtil::uint64Value(length);
      }
    } else {
      return ExprValueUtil::uint64Value(stream_info_.bytesReceived());
    }
    break;
  case RequestToken::TOTAL_SIZE:
    return ExprValueUtil::uint64Value(
        stream_info_.bytesReceived() + headers != nullptr ? headers->byteSize() : 0);
  }
  return ExprValueUtil::nullValue();
}

absl::optional<Value> AttrState::responseSet(absl::string_view path) {
  auto attr_fields = getOrInsert(RESPONSE_TOKEN);

  auto part_token = response_tokens.find(path);
  if (part_token == response_tokens.end()) {
    ENVOY_LOG(debug, "Unable to find response attribute: '{}'", path);
    return absl::nullopt;
  }

  switch (part_token->second) {
  case ResponseToken::CODE:
    if (stream_info_.responseCode().has_value()) {
      return ExprValueUtil::uint64Value(stream_info_.responseCode().value());
    }
    break;
  case ResponseToken::CODE_DETAILS:
    return ExprValueUtil::optionalStringValue(stream_info_.responseCodeDetails());
  case ResponseToken::FLAGS:
    return ExprValueUtil::uint64Value(stream_info_.responseFlags());
  case ResponseToken::GRPC_STATUS:
    ENVOY_LOG(debug, "ignoring unimplemented attribute response.grpc_status");
    break;
  case ResponseToken::HEADERS:
    ENVOY_LOG(debug, "ignoring unimplemented attribute response.headers");
    break;
  case ResponseToken::TRAILERS:
    ENVOY_LOG(debug, "ignoring unimplemented attribute response.trailers");
    break;
  case ResponseToken::SIZE:
    return ExprValueUtil::uint64Value(stream_info_.bytesSent());

  case ResponseToken::TOTAL_SIZE:
    return ExprValueUtil::uint64Value(stream_info_.bytesReceived());
  }
  return absl::nullopt;
}

absl::optional<Value> AttrState::destinationSet(absl::string_view path) {
  auto attr_fields = getOrInsert(DESTINATION_TOKEN);

  auto addr = stream_info_.downstreamAddressProvider().localAddress();
  if (addr == nullptr) {
    return absl::nullopt;
  }
  if (path == DESTINATION_ADDRESS_TOKEN) {
    return ExprValueUtil::stringValue(addr->asString());
  } else if (path == DESTINATION_PORT_TOKEN) {
    if (addr->ip() == nullptr) {
      return absl::nullopt;
    }
    return ExprValueUtil::uint64Value(addr->ip()->port());
  } else {
    ENVOY_LOG(debug, "Unable to find ext_proc destination attribute: '{}'", path);
  }
  return absl::nullopt;
}

absl::optional<Value> AttrState::sourceSet(absl::string_view path) {
  if (!attributes_.contains(SOURCE_TOKEN)) {
    attributes_[SOURCE_TOKEN] = ProtobufWkt::Struct();
  }
  auto attr_fields = *attributes_[SOURCE_TOKEN].mutable_fields();

  if (stream_info_.upstreamHost() == nullptr) {
    return absl::nullopt;
  }
  auto addr = stream_info_.upstreamHost()->address();
  if (addr == nullptr) {
    return absl::nullopt;
  }
  if (path == SOURCE_ADDRESS_TOKEN) {
    return ExprValueUtil::stringValue(addr->asString());
  } else if (path == SOURCE_PORT_TOKEN) {
    if (addr->ip() == nullptr) {
      return absl::nullopt;
    }
    return ExprValueUtil::uint64Value(addr->ip()->port());
  } else {
    ENVOY_LOG(debug, "Unable to find ext_proc source attribute: '{}'", path);
  }
  return absl::nullopt;
}

absl::optional<Value> AttrState::upstreamSet(absl::string_view path) {
  auto attr_fields = getOrInsert(UPSTREAM_TOKEN);

  auto part_token = upstream_tokens.find(path);
  if (part_token == upstream_tokens.end()) {
    ENVOY_LOG(debug, "Unable to find ext_proc upstream attribute: '{}'", path);
    return absl::nullopt;
  }

  auto upstreamHost = stream_info_.upstreamHost();
  auto upstreamSsl = stream_info_.upstreamSslConnection();
  switch (part_token->second) {
  case UpstreamToken::ADDRESS:
    if (upstreamHost != nullptr && upstreamHost->address() != nullptr) {
      return ExprValueUtil::stringValue(upstreamHost->address()->asString());
    }
    break;
  case UpstreamToken::PORT:
    if (upstreamHost != nullptr && upstreamHost->address() != nullptr &&
        upstreamHost->address()->ip() != nullptr) {
      return ExprValueUtil::uint64Value(upstreamHost->address()->ip()->port());
    }
    break;
  case UpstreamToken::TLS_VERSION:
    if (upstreamSsl != nullptr) {
      return ExprValueUtil::stringValue(upstreamSsl->tlsVersion());
    }
    break;
  case UpstreamToken::SUBJECT_LOCAL_CERTIFICATE:
    if (upstreamSsl != nullptr) {
      return ExprValueUtil::stringValue(upstreamSsl->subjectLocalCertificate());
    }
    break;
  case UpstreamToken::SUBJECT_PEER_CERTIFICATE:
    if (upstreamSsl != nullptr) {
      return ExprValueUtil::stringValue(upstreamSsl->subjectPeerCertificate());
    }
    break;
  case UpstreamToken::DNS_SAN_LOCAL_CERTIFICATE:
    if (upstreamSsl != nullptr) {
      return ExprValueUtil::stringValue(upstreamSsl->dnsSansLocalCertificate().front());
    }
    break;
  case UpstreamToken::DNS_SAN_PEER_CERTIFICATE:
    if (upstreamSsl != nullptr) {
      return ExprValueUtil::stringValue(upstreamSsl->dnsSansPeerCertificate().front());
    }
    break;
  case UpstreamToken::URI_SAN_LOCAL_CERTIFICATE:
    if (upstreamSsl != nullptr) {
      return ExprValueUtil::stringValue(upstreamSsl->uriSanLocalCertificate().front());
    }
    break;
  case UpstreamToken::URI_SAN_PEER_CERTIFICATE:
    if (upstreamSsl != nullptr) {
      return ExprValueUtil::stringValue(upstreamSsl->uriSanPeerCertificate().front());
    }
    break;
  case UpstreamToken::LOCAL_ADDRESS:
    if (stream_info_.upstreamLocalAddress() != nullptr) {
      return ExprValueUtil::stringValue(stream_info_.upstreamLocalAddress()->asString());
    }
    break;
  case UpstreamToken::TRANSPORT_FAILURE_REASON:
    return ExprValueUtil::stringValue(stream_info_.upstreamTransportFailureReason());
    break;
  }
  return absl::nullopt;
}

absl::optional<Value> AttrState::connectionSet(absl::string_view path) {
  auto attr_fields = getOrInsert(CONNECTION_TOKEN);

  auto part_token = connection_tokens.find(path);
  if (part_token == connection_tokens.end()) {
    ENVOY_LOG(debug, "Unable to find ext_proc connection attribute: '{}'", path);
    return absl::nullopt;
  }

  auto connId = stream_info_.connectionID();
  auto downstreamSsl = stream_info_.downstreamSslConnection();

  switch (part_token->second) {
  case ConnectionToken::ID:
    if (connId.has_value()) {
      return ExprValueUtil::uint64Value(connId.value());
    }
    break;
  case ConnectionToken::MTLS:
    return absl::nullopt;
    if (downstreamSsl != nullptr) {
      return ExprValueUtil::boolValue(downstreamSsl->peerCertificatePresented());
    }
    break;
  case ConnectionToken::REQUESTED_SERVER_NAME:
    return ExprValueUtil::stringValue(stream_info_.requestedServerName());
  case ConnectionToken::TLS_VERSION:
    if (downstreamSsl != nullptr) {
      return ExprValueUtil::stringValue(downstreamSsl->tlsVersion());
    }
    break;
  case ConnectionToken::SUBJECT_LOCAL_CERTIFICATE:
    if (downstreamSsl != nullptr) {
      return ExprValueUtil::stringValue(downstreamSsl->subjectLocalCertificate());
    }
    break;
  case ConnectionToken::SUBJECT_PEER_CERTIFICATE:
    if (downstreamSsl != nullptr) {
      return ExprValueUtil::stringValue(downstreamSsl->subjectPeerCertificate());
    }
    break;
  case ConnectionToken::DNS_SAN_LOCAL_CERTIFICATE:
    if (downstreamSsl != nullptr) {
      return ExprValueUtil::stringValue(
          std::string(downstreamSsl->dnsSansLocalCertificate().front()));
    }
    break;
  case ConnectionToken::DNS_SAN_PEER_CERTIFICATE:
    if (downstreamSsl != nullptr) {
      return ExprValueUtil::stringValue(
          std::string(downstreamSsl->dnsSansPeerCertificate().front()));
    }
    break;
  case ConnectionToken::URI_SAN_LOCAL_CERTIFICATE:
    if (downstreamSsl != nullptr) {
      return ExprValueUtil::stringValue(
          std::string(downstreamSsl->uriSanLocalCertificate().front()));
    }
    break;
  case ConnectionToken::URI_SAN_PEER_CERTIFICATE:
    if (downstreamSsl != nullptr) {
      return ExprValueUtil::stringValue(
          std::string(downstreamSsl->uriSanPeerCertificate().front()));
    }
    break;
  case ConnectionToken::TERMINATION_DETAILS:
    if (downstreamSsl != nullptr) {
      return ExprValueUtil::optionalStringValue(stream_info_.connectionTerminationDetails());
    }
  }
  return absl::nullopt;
}

absl::optional<Value> AttrState::metadataSet() {
  if (attributes_.contains(METADATA_TOKEN)) {
    MapValue m;
    for (auto const& [k, v] : stream_info_.dynamicMetadata().filter_metadata()) {
      Value sk = ExprValueUtil::stringValue(k);
      // todo: convert s to object value (any)
      MapValue_Entry* e = m.add_entries();
      e->set_allocated_key(&sk);
      Value vv;
      Value any_v;
      Any* a = any_v.mutable_object_value();
      a->PackFrom(v);
      vv.set_allocated_object_value(a);
      e->set_allocated_value(&vv);
    }
    return absl::make_optional(ExprValueUtil::mapValue(&m));
  }
  return absl::nullopt;
}

// todo(eas): there are two issues here
// 1. FilterState seems to be an opaque data store and therefore does not have an iterator
// implemented on it,
//   it seems incorrect to just attach all of this data, need to check with envoy maintainers.
// 2. Encoding as a
// [`ProtobufWkt::Value`](https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#value)
//   is problematic. In the [attribute
//   docs](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/advanced/attributes) it
//   is indicated that the filter state values should be binary data, but `Value` only allows the
//   following: [null, number, string, bool, struct, list] where struct is simply a `map<string,
//   value>`.
absl::optional<Value> AttrState::filterStateSet() {
  ENVOY_LOG(debug, "ignoring unimplemented attribute filter_state");
  return absl::nullopt;
}

std::string AttrState::formatDuration(absl::Duration duration) {
  return absl::FormatDuration(duration);
}

std::string AttrState::getTs() {
  ProtobufWkt::Timestamp ts;
  TimestampUtil::systemClockToTimestamp(stream_info_.startTime(), ts);
  return Protobuf::util::TimeUtil::ToString(ts);
}

ProtobufWkt::Map<std::string, ProtobufWkt::Value>& AttrState::getOrInsert(std::string key) {
  if (attributes_.contains(key)) {
    attributes_[key] = ProtobufWkt::Struct();
  }
  return *attributes_[key].mutable_fields();
}

// todo(eas): this seems to result in a nullptr exception when empty headers are used.
// todo(eas): these are unavailable at headers time.
//
absl::optional<Value> AttrState::getGrpcStatus() {
  Http::ResponseHeaderMap& hs = response_headers_ != nullptr
                                    ? *response_headers_
                                    : *Envoy::Http::StaticEmptyHeaders::get().response_headers;

  // Http::ResponseTrailerMap& ts = response_trailers_ != nullptr
  //                                    ? *response_trailers_
  //                                    : *Envoy::Http::StaticEmptyHeaders::get().response_trailers;

  if (!Envoy::Grpc::Common::hasGrpcContentType(hs)) {
    return absl::nullopt;
  }

  auto const& optional_status = Envoy::Grpc::Common::getGrpcStatus(ts, hs, stream_info_);

  if (optional_status.has_value()) {
    return ExprValueUtil::uint64Value(optional_status.value());
  }
  return absl::nullopt;
}
void AttrState::setRequestHeaders(Http::RequestHeaderMap* request_headers) {
  request_headers_ = request_headers;
}
void AttrState::setResponseHeaders(Http::ResponseHeaderMap* response_headers) {
  response_headers_ = response_headers;
}
