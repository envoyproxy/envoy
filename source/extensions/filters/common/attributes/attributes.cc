#include "source/extensions/filters/common/attributes/attributes.h"

#include <atomic>
#include <chrono>
#include <iterator>
#include <map>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/network/filter.h"
#include "envoy/network/socket.h"
#include "envoy/stats/sink.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/logger.h"
#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/strings/str_format.h"
#include "id.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Attributes {

using HashPolicy = envoy::config::route::v3::RouteAction::HashPolicy;
using HeaderMap = Http::HeaderMap;
using HeaderEntry = Http::HeaderEntry;

using MapValue = google::api::expr::v1alpha1::MapValue;
using MapEntry = google::api::expr::v1alpha1::MapValue_Entry;
using Value = google::api::expr::v1alpha1::Value;
using NullValue = Envoy::ProtobufWkt::NullValue;
using Any = Envoy::ProtobufWkt::Any;

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    referer_handle = Http::CustomHeaders::get().Referer;

MapEntry* ValueUtil::getOrInsert(MapValue* m, absl::string_view key) {
  if (m == nullptr) {
    return nullptr;
  }
  auto entries = m->mutable_entries();
  for (auto iter = entries->pointer_begin(); iter < entries->pointer_end(); iter++) {
    auto entry = *iter;
    if (entry->has_key() && entry->key().has_string_value() && entry->key().string_value() == key) {
      return entry;
    }
  }

  // add a new entry to entries
  MapEntry* e = m->add_entries();
  auto k = e->mutable_key();
  k->set_string_value(std::string(key));

  return e;
}

Value* ValueUtil::stringValue(const std::string& str) {
  Value* val = new Value();
  val->set_string_value(str);
  return val;
}

Value* ValueUtil::uint64Value(uint64_t n) {
  Value* val = new Value();
  val->set_uint64_value(n);
  return val;
}

Value* ValueUtil::boolValue(bool b) {
  Value* val = new Value();
  val->set_bool_value(b);
  return val;
}

Value* ValueUtil::nullValue() {
  Value* v = new Value();
  v->set_null_value(v->null_value());
  return v;
}

template <class T> Value* ValueUtil::objectValue(T val) {
  Value* v = new Value();
  Any* a = v->mutable_object_value();
  auto ok = a->PackFrom(*val);
  if (!ok) {
    ENVOY_LOG(trace, "Failed to serialize object {}", val);
  }

  return v;
}

Value* Attributes::buildAttributesValue(const std::vector<AttributeId>& attrs) {
  Value* value = new Value();
  auto map = value->mutable_map_value();

  for (auto attr : attrs) {
    RootToken root_token = attr.root();
    MapEntry* root_entry = ValueUtil::getOrInsert(map, attr.rootName());

    if (!attr.sub()) {
      Value* val = full(root_token);
      root_entry->set_allocated_value(val);
    } else {
      Value* root_entry_val = root_entry->mutable_value();
      MapValue* sub_map = root_entry_val->mutable_map_value();

      auto sub_key = attr.subName();
      MapEntry* sub_entry = ValueUtil::getOrInsert(sub_map, *sub_key);
      Value* sub_val = get(attr);

      sub_entry->set_allocated_value(sub_val);
    }
  }

  return value;
}

Value* Attributes::get(AttributeId& attr_id) {
  ENVOY_LOG(trace, "getting attr_id: {}", attr_id.rootName());

  switch (attr_id.root()) {
  case RootToken::REQUEST: {
    RequestToken tok;
    attr_id.sub(tok);
    return get(tok);
  }
  case RootToken::RESPONSE: {
    ResponseToken tok;
    attr_id.sub(tok);
    return get(tok);
  }
  case RootToken::SOURCE: {
    SourceToken tok;
    attr_id.sub(tok);
    return get(tok);
  }
  case RootToken::DESTINATION: {
    DestinationToken tok;
    attr_id.sub(tok);
    return get(tok);
  }
  case RootToken::CONNECTION: {
    ConnectionToken tok;
    attr_id.sub(tok);
    return get(tok);
  }
  case RootToken::UPSTREAM: {
    UpstreamToken tok;
    attr_id.sub(tok);
    return get(tok);
  }
  case RootToken::METADATA:
  case RootToken::FILTER_STATE:
    return ValueUtil::nullValue();
  }

  return ValueUtil::nullValue();
}

Value* Attributes::full(RootToken tok) {
  ENVOY_LOG(trace, "full", root_tokens_inv[tok]);

  switch (tok) {
  case RootToken::REQUEST:
    return full<RequestToken>(request_tokens);
  case RootToken::RESPONSE:
    return full<ResponseToken>(response_tokens);
  case RootToken::SOURCE:
    return full<SourceToken>(source_tokens);
  case RootToken::DESTINATION:
    return full<DestinationToken>(destination_tokens);
  case RootToken::CONNECTION:
    return full<ConnectionToken>(connection_tokens);
  case RootToken::UPSTREAM:
    return full<UpstreamToken>(upstream_tokens);
  case RootToken::METADATA:
    return getMetadata();
  case RootToken::FILTER_STATE:
    return getFilterState();
  }
  return ValueUtil::nullValue();
}
template <class T> Value* Attributes::full(absl::flat_hash_map<std::string, T>& tokens) {
  Value* v = new Value();
  MapValue* m = v->mutable_map_value();

  for (auto& element : tokens) {
    Value* val = get(element.second);
    MapEntry* e = m->add_entries();
    auto key = e->mutable_key();
    key->set_string_value(element.first);

    auto value = e->mutable_value();
    value->CopyFrom(*val);
    delete val;
  }

  return v;
}

Value* Attributes::get(RequestToken tok) {
  ENVOY_LOG(trace, "request get: {}", request_tokens_inv[tok]);

  // Attributes that do not *require* request_headers.
  switch (tok) {
  case RequestToken::TIME:
    return ValueUtil::stringValue(getTs());
  case RequestToken::PROTOCOL: {
    auto proto = stream_info_.protocol();
    if (proto.has_value()) {
      return ValueUtil::stringValue(HttpProtocolStrings[static_cast<int>(proto.value())]);
    }
    return ValueUtil::nullValue();
  }
  case RequestToken::DURATION: {
    auto complete = stream_info_.requestComplete();
    if (complete.has_value()) {
      return ValueUtil::stringValue(formatDuration(absl::FromChrono(complete.value())));
    }
    return ValueUtil::nullValue();
  }
  case RequestToken::SIZE:
    if (request_headers_ != nullptr && request_headers_->ContentLength() != nullptr) {
      int64_t length;
      if (absl::SimpleAtoi(request_headers_->ContentLength()->value().getStringView(), &length)) {
        return ValueUtil::uint64Value(length);
      }
    } else {
      return ValueUtil::uint64Value(stream_info_.bytesReceived());
    }
    break;
  case RequestToken::TOTAL_SIZE: {
    auto headers_size = request_headers_ != nullptr ? request_headers_->byteSize() : 0;
    auto trailers_size = request_trailers_ != nullptr ? request_trailers_->byteSize() : 0;

    return ValueUtil::uint64Value(stream_info_.bytesReceived() + headers_size + trailers_size);
  }
  default:
    break;
  }

  if (request_headers_ == nullptr) {
    return ValueUtil::nullValue();
  }
  switch (tok) {
  case RequestToken::PATH:
    return ValueUtil::stringValue(std::string(request_headers_->getPathValue()));
  case RequestToken::URL_PATH: {
    if (request_headers_->Path() != nullptr && request_headers_->Path()->value() != nullptr) {
      auto path = request_headers_->Path()->value().getStringView();
      auto end = std::max(path.find('\0'), path.find('?'));
      return ValueUtil::stringValue(std::string(path.substr(0, end)));
    }
    return ValueUtil::stringValue("");
  }
  case RequestToken::HOST:
    return ValueUtil::stringValue(std::string(request_headers_->getHostValue()));
  case RequestToken::SCHEME:
    return ValueUtil::stringValue(std::string(request_headers_->getSchemeValue()));
  case RequestToken::METHOD:
    return ValueUtil::stringValue(std::string(request_headers_->getMethodValue()));
  case RequestToken::HEADERS:
    return getRequestHeaders();
  case RequestToken::REFERER:
    return ValueUtil::stringValue(
        std::string(request_headers_->getInlineValue(referer_handle.handle())));
  case RequestToken::USERAGENT:
    return ValueUtil::stringValue(std::string(request_headers_->getUserAgentValue()));
  case RequestToken::ID:
    return ValueUtil::stringValue(std::string(request_headers_->getRequestIdValue()));
  default:
    break;
  }
  return ValueUtil::nullValue();
}

Value* Attributes::get(ResponseToken tok) {
  ENVOY_LOG(trace, "response get: {}", response_tokens_inv[tok]);

  switch (tok) {
  case ResponseToken::CODE: {
    auto code = stream_info_.responseCode();
    if (code.has_value()) {
      return ValueUtil::uint64Value(code.value());
    }
    return ValueUtil::nullValue();
  }
  case ResponseToken::CODE_DETAILS: {
    auto details = stream_info_.responseCodeDetails();
    if (details.has_value()) {
      return ValueUtil::stringValue(details.value());
    }
    return ValueUtil::nullValue();
  }
  case ResponseToken::FLAGS:
    return ValueUtil::uint64Value(stream_info_.responseFlags());
  case ResponseToken::GRPC_STATUS:
    return getResponseGrpcStatus();
  case ResponseToken::HEADERS:
    return getResponseHeaders();
  case ResponseToken::TRAILERS:
    return getResponseTrailers();
  case ResponseToken::SIZE:
    return ValueUtil::uint64Value(stream_info_.bytesSent());
  case ResponseToken::TOTAL_SIZE: {
    auto headers_size = response_headers_ != nullptr ? response_headers_->byteSize() : 0;
    auto trailers_size = response_trailers_ != nullptr ? response_trailers_->byteSize() : 0;

    return ValueUtil::uint64Value(stream_info_.bytesSent() + headers_size + trailers_size);
  }
  }
  return ValueUtil::nullValue();
}

Value* Attributes::get(SourceToken tok) {
  ENVOY_LOG(trace, "source get: {}", source_tokens_inv[tok]);

  auto host = stream_info_.upstreamHost();
  if (host == nullptr) {
    return ValueUtil::nullValue();
  }
  auto addr = host->address();
  if (addr == nullptr) {
    return ValueUtil::nullValue();
  }

  switch (tok) {
  case SourceToken::ADDRESS:
    return ValueUtil::stringValue(addr->asString());
  case SourceToken::PORT:
    if (addr->ip() == nullptr) {
      return ValueUtil::nullValue();
    }
    return ValueUtil::uint64Value(addr->ip()->port());
  }
  return ValueUtil::nullValue();
}

Value* Attributes::get(DestinationToken tok) {
  ENVOY_LOG(trace, "destination get: {}", destination_tokens_inv[tok]);

  auto addr = stream_info_.downstreamAddressProvider().localAddress();
  if (addr == nullptr) {
    return ValueUtil::nullValue();
  }

  switch (tok) {
  case DestinationToken::ADDRESS:
    return ValueUtil::stringValue(addr->asString());
  case DestinationToken::PORT:
    if (addr->ip() == nullptr) {
      return ValueUtil::nullValue();
    }
    return ValueUtil::uint64Value(addr->ip()->port());
  }
  return ValueUtil::nullValue();
}

Value* Attributes::get(UpstreamToken tok) {
  ENVOY_LOG(trace, "upstream get: {}", upstream_tokens_inv[tok]);

  auto upstreamHost = stream_info_.upstreamHost();

  switch (tok) {
  case UpstreamToken::ADDRESS:
    if (upstreamHost != nullptr && upstreamHost->address() != nullptr) {
      return ValueUtil::stringValue(upstreamHost->address()->asString());
    }
    return ValueUtil::nullValue();
  case UpstreamToken::PORT:
    if (upstreamHost != nullptr && upstreamHost->address() != nullptr &&
        upstreamHost->address()->ip() != nullptr) {
      return ValueUtil::uint64Value(upstreamHost->address()->ip()->port());
    }
    return ValueUtil::nullValue();
  case UpstreamToken::LOCAL_ADDRESS: {
    const Network::Address::InstanceConstSharedPtr& addr = stream_info_.upstreamLocalAddress();
    if (addr && addr.get() != nullptr && addr.use_count() != 0) {
      std::cerr << addr.get();
      std::cerr << addr.use_count();
      const std::string& s = addr.get()->asString();
      return ValueUtil::stringValue(s);
    }
    return ValueUtil::nullValue();
  }
  case UpstreamToken::TRANSPORT_FAILURE_REASON:
    return ValueUtil::stringValue(stream_info_.upstreamTransportFailureReason());
  default:
    break;
  }

  // Here we'll handle the attributes that depend on a non-null upstreamSslConnection.
  auto upstreamSsl = stream_info_.upstreamSslConnection();
  if (upstreamSsl == nullptr) {
    return ValueUtil::nullValue();
  }

  switch (tok) {
  case UpstreamToken::TLS_VERSION:
    return ValueUtil::stringValue(upstreamSsl->tlsVersion());
  case UpstreamToken::SUBJECT_LOCAL_CERTIFICATE:
    return ValueUtil::stringValue(upstreamSsl->subjectLocalCertificate());
  case UpstreamToken::SUBJECT_PEER_CERTIFICATE:
    return ValueUtil::stringValue(upstreamSsl->subjectPeerCertificate());
  case UpstreamToken::DNS_SAN_LOCAL_CERTIFICATE:
    return ValueUtil::stringValue(upstreamSsl->dnsSansLocalCertificate().front());
  case UpstreamToken::DNS_SAN_PEER_CERTIFICATE:
    return ValueUtil::stringValue(upstreamSsl->dnsSansPeerCertificate().front());
  case UpstreamToken::URI_SAN_LOCAL_CERTIFICATE:
    return ValueUtil::stringValue(upstreamSsl->uriSanLocalCertificate().front());
  case UpstreamToken::URI_SAN_PEER_CERTIFICATE:
    return ValueUtil::stringValue(upstreamSsl->uriSanPeerCertificate().front());
  default:
    return ValueUtil::nullValue();
  }
}

Value* Attributes::get(ConnectionToken tok) {
  ENVOY_LOG(trace, "connection get: {}", connection_tokens_inv[tok]);
  const auto& addr_provider = stream_info_.downstreamAddressProvider();

  // Get the tokens out of the way that do not need access to the downstreamSslConnection.
  if (tok == ConnectionToken::ID) {
    auto id = addr_provider.connectionID();
    if (id.has_value()) {
      return ValueUtil::uint64Value(id.value());
    } else {
      return ValueUtil::nullValue();
    }
  } else if (tok == ConnectionToken::REQUESTED_SERVER_NAME) {
    auto sn = addr_provider.requestedServerName();
    return ValueUtil::stringValue(std::string(sn));
  } else if (tok == ConnectionToken::TERMINATION_DETAILS) {
    const absl::optional<std::string> details = stream_info_.connectionTerminationDetails();
    if (details.has_value()) {
      return ValueUtil::stringValue(details.value());
    }
    return ValueUtil::nullValue();
  }

  auto downstreamSsl = addr_provider.sslConnection();
  if (downstreamSsl == nullptr) {
    return ValueUtil::nullValue();
  }

  switch (tok) {
  case ConnectionToken::MTLS:
    return ValueUtil::boolValue(downstreamSsl->peerCertificatePresented());
  case ConnectionToken::TLS_VERSION:
    return ValueUtil::stringValue(downstreamSsl->tlsVersion());
  case ConnectionToken::SUBJECT_LOCAL_CERTIFICATE:
    return ValueUtil::stringValue(downstreamSsl->subjectLocalCertificate());
  case ConnectionToken::SUBJECT_PEER_CERTIFICATE:
    return ValueUtil::stringValue(downstreamSsl->subjectPeerCertificate());
  case ConnectionToken::DNS_SAN_LOCAL_CERTIFICATE:
    return ValueUtil::stringValue(std::string(downstreamSsl->dnsSansLocalCertificate().front()));
  case ConnectionToken::DNS_SAN_PEER_CERTIFICATE:
    return ValueUtil::stringValue(std::string(downstreamSsl->dnsSansPeerCertificate().front()));
  case ConnectionToken::URI_SAN_LOCAL_CERTIFICATE:
    return ValueUtil::stringValue(std::string(downstreamSsl->uriSanLocalCertificate().front()));
  case ConnectionToken::URI_SAN_PEER_CERTIFICATE:
    return ValueUtil::stringValue(std::string(downstreamSsl->uriSanPeerCertificate().front()));
  default:
    return ValueUtil::nullValue();
  }
}

Value* Attributes::getMetadata() {
  ENVOY_LOG(trace, "metadata get");

  auto v = new Value();
  auto m = v->mutable_map_value();

  for (auto const& [k, v] : stream_info_.dynamicMetadata().filter_metadata()) {
    MapEntry* e = m->add_entries();
    auto key = e->mutable_key();
    key->set_string_value(k);

    auto val = e->mutable_value();
    auto obj = val->mutable_object_value();

    auto ok = obj->PackFrom(v);
    if (!ok) {
      ENVOY_LOG(trace, "Failed to serialize metadata value with key: {}", k);

      val->set_null_value(val->null_value());
    }
  }
  return v;
}

Value* Attributes::getFilterState() { return ValueUtil::nullValue(); }

std::string Attributes::formatDuration(absl::Duration duration) {
  return absl::FormatDuration(duration);
}

std::string Attributes::getTs() {
  ProtobufWkt::Timestamp ts;
  TimestampUtil::systemClockToTimestamp(stream_info_.startTime(), ts);
  return Protobuf::util::TimeUtil::ToString(ts);
}

Value* Attributes::getResponseGrpcStatus() {
  using GrpcCommon = Envoy::Grpc::Common;

  // This is not a trailers only response but content-type is not set as grpc so not a grpc request.
  if (response_headers_ != nullptr && !GrpcCommon::hasGrpcContentType(*response_headers_)) {
    return ValueUtil::nullValue();
  }

  const Http::ResponseHeaderMap& hs =
      response_headers_ != nullptr ? *response_headers_
                                   : *Envoy::Http::StaticEmptyHeaders::get().response_headers;
  const Http::ResponseTrailerMap& ts =
      response_trailers_ != nullptr ? *response_trailers_
                                    : *Envoy::Http::StaticEmptyHeaders::get().response_trailers;
  auto const& optional_status = GrpcCommon::getGrpcStatus(ts, hs, stream_info_);

  if (optional_status.has_value()) {
    return ValueUtil::uint64Value(optional_status.value());
  }
  return ValueUtil::nullValue();
}

Value* Attributes::getRequestHeaders() {
  if (request_headers_ == nullptr) {
    return ValueUtil::nullValue();
  }

  auto v = new Value();
  auto m = v->mutable_map_value();

  request_headers_->iterate([m](const HeaderEntry& header) -> HeaderMap::Iterate {
    auto e = m->add_entries();
    auto key = e->mutable_key();
    key->set_string_value(std::string(header.key().getStringView()));

    auto value = e->mutable_value();
    value->set_string_value(std::string(header.value().getStringView()));

    return HeaderMap::Iterate::Continue;
  });
  return v;
}

Value* Attributes::getResponseHeaders() {
  if (response_headers_ == nullptr) {
    return ValueUtil::nullValue();
  }
  auto v = new Value();
  auto m = v->mutable_map_value();

  response_headers_->iterate([m](const HeaderEntry& header) -> HeaderMap::Iterate {
    auto e = m->add_entries();
    auto key = e->mutable_key();
    key->set_string_value(std::string(header.key().getStringView()));

    auto value = e->mutable_value();
    value->set_string_value(std::string(header.value().getStringView()));

    return HeaderMap::Iterate::Continue;
  });
  return v;
}

Value* Attributes::getResponseTrailers() {
  if (response_trailers_ == nullptr) {
    return ValueUtil::nullValue();
  }
  auto v = new Value();
  auto m = v->mutable_map_value();

  response_trailers_->iterate([m](const HeaderEntry& header) -> HeaderMap::Iterate {
    auto e = m->add_entries();
    auto key = e->mutable_key();
    key->set_string_value(std::string(header.key().getStringView()));

    auto value = e->mutable_value();
    value->set_string_value(std::string(header.value().getStringView()));

    return HeaderMap::Iterate::Continue;
  });
  return v;
}

} // namespace Attributes
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
