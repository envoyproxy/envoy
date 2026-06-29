#include "source/extensions/dynamic_modules/abi_context_accessors.h"

#include <functional>
#include <string>
#include <vector>

#include "source/common/common/logger.h"
#include "source/common/config/metadata.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

namespace {

// Extract a downstream SSL string attribute from the stream info.
bool getDownstreamSslAttribute(
    const StreamInfo::StreamInfo& stream_info,
    std::function<OptRef<const std::string>(const Ssl::ConnectionInfoConstSharedPtr)> extractor,
    envoy_dynamic_module_type_envoy_buffer* result) {
  const auto& provider = stream_info.downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }
  const Ssl::ConnectionInfoConstSharedPtr ssl = provider.sslConnection();
  OptRef<const std::string> attr = extractor(ssl);
  if (!attr.has_value() || attr->empty()) {
    return false;
  }
  const std::string& value = attr.value();
  *result = {const_cast<char*>(value.data()), value.size()};
  return true;
}

// Extract an upstream SSL string attribute from the stream info.
bool getUpstreamSslAttribute(
    const StreamInfo::StreamInfo& stream_info,
    std::function<OptRef<const std::string>(const Ssl::ConnectionInfoConstSharedPtr)> extractor,
    envoy_dynamic_module_type_envoy_buffer* result) {
  const auto upstream = stream_info.upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return false;
  }
  const Ssl::ConnectionInfoConstSharedPtr ssl = upstream->upstreamSslConnection();
  OptRef<const std::string> attr = extractor(ssl);
  if (!attr.has_value() || attr->empty()) {
    return false;
  }
  const std::string& value = attr.value();
  *result = {const_cast<char*>(value.data()), value.size()};
  return true;
}

// Resolve a dynamic metadata value by filter name and dotted key path. Returns an unset value
// when the path is absent.
const Protobuf::Value& dynamicMetadataValue(const StreamInfo::StreamInfo& stream_info,
                                            envoy_dynamic_module_type_module_buffer filter_name,
                                            envoy_dynamic_module_type_module_buffer path) {
  std::string filter_name_str(filter_name.ptr, filter_name.length);
  std::string path_str(path.ptr, path.length);
  std::vector<std::string> path_parts = absl::StrSplit(path_str, '.');
  const auto& metadata = stream_info.dynamicMetadata();
  return Envoy::Config::Metadata::metadataValue(&metadata, filter_name_str, path_parts);
}

} // namespace

HeadersMapOptConstRef
ContextAccessor::headerMapByType(const Formatter::Context& context,
                                 envoy_dynamic_module_type_http_header_type type) {
  switch (type) {
  case envoy_dynamic_module_type_http_header_type_RequestHeader:
    return context.requestHeaders();
  case envoy_dynamic_module_type_http_header_type_ResponseHeader:
    return context.responseHeaders();
  case envoy_dynamic_module_type_http_header_type_ResponseTrailer:
    return context.responseTrailers();
  default:
    return {};
  }
}

bool ContextAccessor::getHeaders(HeadersMapOptConstRef map,
                                 envoy_dynamic_module_type_envoy_http_header* result_headers) {
  if (!map) {
    return false;
  }
  size_t i = 0;
  map->iterate([&i, &result_headers](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    auto& key = header.key();
    result_headers[i].key_ptr = const_cast<char*>(key.getStringView().data());
    result_headers[i].key_length = key.size();
    auto& value = header.value();
    result_headers[i].value_ptr = const_cast<char*>(value.getStringView().data());
    result_headers[i].value_length = value.size();
    i++;
    return Http::HeaderMap::Iterate::Continue;
  });
  return true;
}

bool ContextAccessor::getHeaderValue(HeadersMapOptConstRef map,
                                     envoy_dynamic_module_type_module_buffer key,
                                     envoy_dynamic_module_type_envoy_buffer* result, size_t index,
                                     size_t* total_count_out) {
  if (!map.has_value()) {
    *result = {.ptr = nullptr, .length = 0};
    if (total_count_out != nullptr) {
      *total_count_out = 0;
    }
    return false;
  }
  absl::string_view key_view(key.ptr, key.length);

  // Note: We convert to LowerCaseString which may involve copying. This could be optimized if
  // callers guarantee lowercase keys.
  const auto values = map->get(Envoy::Http::LowerCaseString(key_view));
  if (total_count_out != nullptr) {
    *total_count_out = values.size();
  }

  if (index >= values.size()) {
    *result = {.ptr = nullptr, .length = 0};
    return false;
  }

  const auto value = values[index]->value().getStringView();
  *result = {.ptr = const_cast<char*>(value.data()), .length = value.size()};
  return true;
}

bool ContextAccessor::getAttributeString(const StreamInfo::StreamInfo& stream_info,
                                         envoy_dynamic_module_type_attribute_id attribute_id,
                                         envoy_dynamic_module_type_envoy_buffer* result) {
  bool ok = false;
  switch (attribute_id) {
  case envoy_dynamic_module_type_attribute_id_RequestProtocol: {
    if (!stream_info.protocol().has_value()) {
      break;
    }
    const auto& protocol_str = Http::Utility::getProtocolString(stream_info.protocol().value());
    *result = {const_cast<char*>(protocol_str.data()), protocol_str.size()};
    ok = true;
    break;
  }
  case envoy_dynamic_module_type_attribute_id_ResponseCodeDetails: {
    if (!stream_info.responseCodeDetails().has_value()) {
      break;
    }
    const auto& details = stream_info.responseCodeDetails().value();
    *result = {const_cast<char*>(details.data()), details.size()};
    ok = true;
    break;
  }
  case envoy_dynamic_module_type_attribute_id_XdsRouteName: {
    const auto& name = stream_info.getRouteName();
    if (!name.empty()) {
      *result = {const_cast<char*>(name.data()), name.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_XdsVirtualHostName: {
    const auto& name = stream_info.virtualClusterName();
    if (name.has_value() && !name->empty()) {
      *result = {const_cast<char*>(name->data()), name->size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestId: {
    const auto provider = stream_info.getStreamIdProvider();
    if (provider.has_value() && provider->toStringView().has_value()) {
      absl::string_view view = provider->toStringView().value();
      *result = {const_cast<char*>(view.data()), view.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_SourceAddress: {
    const auto& addr_provider = stream_info.downstreamAddressProvider();
    if (addr_provider.remoteAddress() &&
        addr_provider.remoteAddress()->type() == Network::Address::Type::Ip) {
      const auto& addr_str = addr_provider.remoteAddress()->ip()->addressAsString();
      *result = {const_cast<char*>(addr_str.data()), addr_str.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_DestinationAddress: {
    const auto& addr_provider = stream_info.downstreamAddressProvider();
    if (addr_provider.localAddress() &&
        addr_provider.localAddress()->type() == Network::Address::Type::Ip) {
      const auto& addr_str = addr_provider.localAddress()->ip()->addressAsString();
      *result = {const_cast<char*>(addr_str.data()), addr_str.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_ConnectionRequestedServerName: {
    const auto& sni = stream_info.downstreamAddressProvider().requestedServerName();
    if (!sni.empty()) {
      *result = {const_cast<char*>(sni.data()), sni.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_ConnectionTerminationDetails: {
    const auto& details = stream_info.connectionTerminationDetails();
    if (details.has_value() && !details->empty()) {
      *result = {const_cast<char*>(details->data()), details->size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_ConnectionTransportFailureReason: {
    const auto& reason = stream_info.downstreamTransportFailureReason();
    if (!reason.empty()) {
      *result = {const_cast<char*>(reason.data()), reason.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_UpstreamAddress: {
    const auto upstream = stream_info.upstreamInfo();
    if (upstream.has_value() && upstream->upstreamHost() &&
        upstream->upstreamHost()->address() != nullptr) {
      auto addr = upstream->upstreamHost()->address()->asStringView();
      *result = {const_cast<char*>(addr.data()), addr.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_UpstreamLocalAddress: {
    const auto upstream = stream_info.upstreamInfo();
    if (upstream.has_value() && upstream->upstreamLocalAddress() != nullptr) {
      auto addr = upstream->upstreamLocalAddress()->asStringView();
      *result = {const_cast<char*>(addr.data()), addr.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_UpstreamTransportFailureReason: {
    const auto upstream = stream_info.upstreamInfo();
    if (upstream.has_value() && !upstream->upstreamTransportFailureReason().empty()) {
      const auto& reason = upstream->upstreamTransportFailureReason();
      *result = {const_cast<char*>(reason.data()), reason.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_ConnectionTlsVersion:
    return getDownstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->tlsVersion();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionSubjectPeerCertificate:
    return getDownstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->subjectPeerCertificate();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionSubjectLocalCertificate:
    return getDownstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->subjectLocalCertificate();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionSha256PeerCertificateDigest:
    return getDownstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->sha256PeerCertificateDigest();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionDnsSanLocalCertificate:
    return getDownstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->dnsSansLocalCertificate().empty()) {
            return std::nullopt;
          }
          return ssl->dnsSansLocalCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionDnsSanPeerCertificate:
    return getDownstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->dnsSansPeerCertificate().empty()) {
            return std::nullopt;
          }
          return ssl->dnsSansPeerCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionUriSanLocalCertificate:
    return getDownstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->uriSanLocalCertificate().empty()) {
            return std::nullopt;
          }
          return ssl->uriSanLocalCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionUriSanPeerCertificate:
    return getDownstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->uriSanPeerCertificate().empty()) {
            return std::nullopt;
          }
          return ssl->uriSanPeerCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamTlsVersion:
    return getUpstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->tlsVersion();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamSubjectPeerCertificate:
    return getUpstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->subjectPeerCertificate();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamSubjectLocalCertificate:
    return getUpstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->subjectLocalCertificate();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamSha256PeerCertificateDigest:
    return getUpstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->sha256PeerCertificateDigest();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamDnsSanLocalCertificate:
    return getUpstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->dnsSansLocalCertificate().empty()) {
            return std::nullopt;
          }
          return ssl->dnsSansLocalCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamDnsSanPeerCertificate:
    return getUpstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->dnsSansPeerCertificate().empty()) {
            return std::nullopt;
          }
          return ssl->dnsSansPeerCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamUriSanLocalCertificate:
    return getUpstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->uriSanLocalCertificate().empty()) {
            return std::nullopt;
          }
          return ssl->uriSanLocalCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamUriSanPeerCertificate:
    return getUpstreamSslAttribute(
        stream_info,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->uriSanPeerCertificate().empty()) {
            return std::nullopt;
          }
          return ssl->uriSanPeerCertificate().front();
        },
        result);
  default:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "Unsupported attribute ID {} as string.",
                        static_cast<int64_t>(attribute_id));
    break;
  }
  return ok;
}

bool ContextAccessor::getAttributeInt(const StreamInfo::StreamInfo& stream_info,
                                      envoy_dynamic_module_type_attribute_id attribute_id,
                                      uint64_t* result) {
  bool ok = false;
  switch (attribute_id) {
  case envoy_dynamic_module_type_attribute_id_ResponseCode: {
    const auto code = stream_info.responseCode();
    if (code.has_value()) {
      *result = code.value();
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_ConnectionId: {
    *result = stream_info.downstreamAddressProvider().connectionID().value_or(0);
    ok = true;
    break;
  }
  case envoy_dynamic_module_type_attribute_id_SourcePort: {
    const auto& addr = stream_info.downstreamAddressProvider().remoteAddress();
    if (addr && addr->type() == Network::Address::Type::Ip) {
      *result = addr->ip()->port();
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_DestinationPort: {
    const auto& addr = stream_info.downstreamAddressProvider().localAddress();
    if (addr && addr->type() == Network::Address::Type::Ip) {
      *result = addr->ip()->port();
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_UpstreamPort: {
    const auto upstream = stream_info.upstreamInfo();
    if (upstream.has_value() && upstream->upstreamHost() &&
        upstream->upstreamHost()->address() != nullptr) {
      auto ip = upstream->upstreamHost()->address()->ip();
      if (ip) {
        *result = ip->port();
        ok = true;
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_UpstreamRequestAttemptCount: {
    *result = stream_info.attemptCount().value_or(0);
    ok = true;
    break;
  }
  default:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "Unsupported attribute ID {} as int.", static_cast<int64_t>(attribute_id));
    break;
  }
  return ok;
}

bool ContextAccessor::getAttributeBool(const StreamInfo::StreamInfo& stream_info,
                                       envoy_dynamic_module_type_attribute_id attribute_id,
                                       bool* result) {
  bool ok = false;
  switch (attribute_id) {
  case envoy_dynamic_module_type_attribute_id_ConnectionMtls: {
    const auto& provider = stream_info.downstreamAddressProvider();
    if (provider.sslConnection()) {
      *result = provider.sslConnection()->peerCertificatePresented();
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_HealthCheck:
    *result = stream_info.healthCheck();
    ok = true;
    break;
  default:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "Unsupported attribute ID {} as bool.", static_cast<int64_t>(attribute_id));
    break;
  }
  return ok;
}

bool ContextAccessor::getDynamicMetadata(const StreamInfo::StreamInfo& stream_info,
                                         envoy_dynamic_module_type_module_buffer filter_name,
                                         envoy_dynamic_module_type_module_buffer path,
                                         envoy_dynamic_module_type_envoy_buffer* result) {
  // String values are returned zero-copy here. Numbers and bools have dedicated typed accessors.
  const auto& value = dynamicMetadataValue(stream_info, filter_name, path);
  if (value.kind_case() == Protobuf::Value::kStringValue) {
    const auto& str = value.string_value();
    *result = {const_cast<char*>(str.data()), str.size()};
    return true;
  }
  return false;
}

bool ContextAccessor::getDynamicMetadataNumber(const StreamInfo::StreamInfo& stream_info,
                                               envoy_dynamic_module_type_module_buffer filter_name,
                                               envoy_dynamic_module_type_module_buffer path,
                                               double* result) {
  const auto& value = dynamicMetadataValue(stream_info, filter_name, path);
  if (value.kind_case() == Protobuf::Value::kNumberValue) {
    *result = value.number_value();
    return true;
  }
  return false;
}

bool ContextAccessor::getDynamicMetadataBool(const StreamInfo::StreamInfo& stream_info,
                                             envoy_dynamic_module_type_module_buffer filter_name,
                                             envoy_dynamic_module_type_module_buffer path,
                                             bool* result) {
  const auto& value = dynamicMetadataValue(stream_info, filter_name, path);
  if (value.kind_case() == Protobuf::Value::kBoolValue) {
    *result = value.bool_value();
    return true;
  }
  return false;
}

bool ContextAccessor::getLocalReplyBody(const Formatter::Context& context,
                                        envoy_dynamic_module_type_envoy_buffer* result) {
  absl::string_view body = context.localReplyBody();
  if (body.empty()) {
    return false;
  }
  *result = {const_cast<char*>(body.data()), body.size()};
  return true;
}

envoy_dynamic_module_type_access_log_type
ContextAccessor::accessLogTypeToAbi(Formatter::AccessLogType log_type) {
  switch (log_type) {
  case Formatter::AccessLogType::TcpUpstreamConnected:
    return envoy_dynamic_module_type_access_log_type_TcpUpstreamConnected;
  case Formatter::AccessLogType::TcpPeriodic:
    return envoy_dynamic_module_type_access_log_type_TcpPeriodic;
  case Formatter::AccessLogType::TcpConnectionEnd:
    return envoy_dynamic_module_type_access_log_type_TcpConnectionEnd;
  case Formatter::AccessLogType::DownstreamStart:
    return envoy_dynamic_module_type_access_log_type_DownstreamStart;
  case Formatter::AccessLogType::DownstreamPeriodic:
    return envoy_dynamic_module_type_access_log_type_DownstreamPeriodic;
  case Formatter::AccessLogType::DownstreamEnd:
    return envoy_dynamic_module_type_access_log_type_DownstreamEnd;
  case Formatter::AccessLogType::UpstreamPoolReady:
    return envoy_dynamic_module_type_access_log_type_UpstreamPoolReady;
  case Formatter::AccessLogType::UpstreamPeriodic:
    return envoy_dynamic_module_type_access_log_type_UpstreamPeriodic;
  case Formatter::AccessLogType::UpstreamEnd:
    return envoy_dynamic_module_type_access_log_type_UpstreamEnd;
  case Formatter::AccessLogType::DownstreamTunnelSuccessfullyEstablished:
    return envoy_dynamic_module_type_access_log_type_DownstreamTunnelSuccessfullyEstablished;
  case Formatter::AccessLogType::UdpTunnelUpstreamConnected:
    return envoy_dynamic_module_type_access_log_type_UdpTunnelUpstreamConnected;
  case Formatter::AccessLogType::UdpPeriodic:
    return envoy_dynamic_module_type_access_log_type_UdpPeriodic;
  case Formatter::AccessLogType::UdpSessionEnd:
    return envoy_dynamic_module_type_access_log_type_UdpSessionEnd;
  default:
    return envoy_dynamic_module_type_access_log_type_NotSet;
  }
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
