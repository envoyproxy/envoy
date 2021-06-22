#include "source/common/http/conn_manager_utility.h"

#include <atomic>
#include <cstdint>
#include <string>

#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/utility.h"
#include "source/common/http/conn_manager_config.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/http/http2/codec_impl.h"
#include "source/common/http/path_utility.h"
#include "source/common/http/utility.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/tracing/http_tracer_impl.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Http {
namespace {

absl::string_view getScheme(absl::string_view forwarded_proto, bool is_ssl) {
  if (HeaderUtility::schemeIsValid(forwarded_proto)) {
    return forwarded_proto;
  }
  return is_ssl ? Headers::get().SchemeValues.Https : Headers::get().SchemeValues.Http;
}

} // namespace
std::string ConnectionManagerUtility::determineNextProtocol(Network::Connection& connection,
                                                            const Buffer::Instance& data) {
  if (!connection.nextProtocol().empty()) {
    return connection.nextProtocol();
  }

  // See if the data we have so far shows the HTTP/2 prefix. We ignore the case where someone sends
  // us the first few bytes of the HTTP/2 prefix since in all public cases we use SSL/ALPN. For
  // internal cases this should practically never happen.
  if (data.startsWith(Http2::CLIENT_MAGIC_PREFIX)) {
    return Utility::AlpnNames::get().Http2;
  }

  return "";
}

ServerConnectionPtr ConnectionManagerUtility::autoCreateCodec(
    Network::Connection& connection, const Buffer::Instance& data,
    ServerConnectionCallbacks& callbacks, Stats::Scope& scope, Random::RandomGenerator& random,
    Http1::CodecStats::AtomicPtr& http1_codec_stats,
    Http2::CodecStats::AtomicPtr& http2_codec_stats, const Http1Settings& http1_settings,
    const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
    uint32_t max_request_headers_kb, uint32_t max_request_headers_count,
    envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
        headers_with_underscores_action) {
  if (determineNextProtocol(connection, data) == Utility::AlpnNames::get().Http2) {
    Http2::CodecStats& stats = Http2::CodecStats::atomicGet(http2_codec_stats, scope);
    return std::make_unique<Http2::ServerConnectionImpl>(
        connection, callbacks, stats, random, http2_options, max_request_headers_kb,
        max_request_headers_count, headers_with_underscores_action);
  } else {
    Http1::CodecStats& stats = Http1::CodecStats::atomicGet(http1_codec_stats, scope);
    return std::make_unique<Http1::ServerConnectionImpl>(
        connection, stats, callbacks, http1_settings, max_request_headers_kb,
        max_request_headers_count, headers_with_underscores_action);
  }
}

ConnectionManagerUtility::MutateRequestHeadersResult ConnectionManagerUtility::mutateRequestHeaders(
    RequestHeaderMap& request_headers, Network::Connection& connection,
    ConnectionManagerConfig& config, const Router::Config& route_config,
    const LocalInfo::LocalInfo& local_info) {
  // If this is a Upgrade request, do not remove the Connection and Upgrade headers,
  // as we forward them verbatim to the upstream hosts.
  if (Utility::isUpgrade(request_headers)) {
    // The current WebSocket implementation re-uses the HTTP1 codec to send upgrade headers to
    // the upstream host. This adds the "transfer-encoding: chunked" request header if the stream
    // has not ended and content-length does not exist. In HTTP1.1, if transfer-encoding and
    // content-length both do not exist this means there is no request body. After transfer-encoding
    // is stripped here, the upstream request becomes invalid. We can fix it by explicitly adding a
    // "content-length: 0" request header here.
    const bool no_body = (!request_headers.TransferEncoding() && !request_headers.ContentLength());
    if (no_body) {
      request_headers.setContentLength(uint64_t(0));
    }
  } else {
    request_headers.removeConnection();
    request_headers.removeUpgrade();
  }

  // Clean proxy headers.
  request_headers.removeEnvoyInternalRequest();
  request_headers.removeKeepAlive();
  request_headers.removeProxyConnection();
  request_headers.removeTransferEncoding();

  // If we are "using remote address" this means that we create/append to XFF with our immediate
  // peer. Cases where we don't "use remote address" include trusted double proxy where we expect
  // our peer to have already properly set XFF, etc.
  Network::Address::InstanceConstSharedPtr final_remote_address;
  bool allow_trusted_address_checks = false;
  const uint32_t xff_num_trusted_hops = config.xffNumTrustedHops();

  if (config.useRemoteAddress()) {
    allow_trusted_address_checks = request_headers.ForwardedFor() == nullptr;
    // If there are any trusted proxies in front of this Envoy instance (as indicated by
    // the xff_num_trusted_hops configuration option), get the trusted client address
    // from the XFF before we append to XFF.
    if (xff_num_trusted_hops > 0) {
      final_remote_address =
          Utility::getLastAddressFromXFF(request_headers, xff_num_trusted_hops - 1).address_;
    }
    // If there aren't any trusted proxies in front of this Envoy instance, or there
    // are but they didn't populate XFF properly, the trusted client address is the
    // source address of the immediate downstream's connection to us.
    if (final_remote_address == nullptr) {
      final_remote_address = connection.addressProvider().remoteAddress();
    }
    if (!config.skipXffAppend()) {
      if (Network::Utility::isLoopbackAddress(*connection.addressProvider().remoteAddress())) {
        Utility::appendXff(request_headers, config.localAddress());
      } else {
        Utility::appendXff(request_headers, *connection.addressProvider().remoteAddress());
      }
    }
    // If the prior hop is not a trusted proxy, overwrite any x-forwarded-proto value it set as
    // untrusted. Alternately if no x-forwarded-proto header exists, add one.
    if (xff_num_trusted_hops == 0 || request_headers.ForwardedProto() == nullptr) {
      request_headers.setReferenceForwardedProto(
          connection.ssl() ? Headers::get().SchemeValues.Https : Headers::get().SchemeValues.Http);
    }
  } else {
    // If we are not using remote address, attempt to pull a valid IPv4 or IPv6 address out of XFF
    // or through an extension. An extension might be needed when XFF doesn't work (e.g. an
    // irregular network).
    //
    // If we find one, it will be used as the downstream address for logging. It may or may not be
    // used for determining internal/external status (see below).
    OriginalIPDetectionParams params = {request_headers,
                                        connection.addressProvider().remoteAddress()};
    for (const auto& detection_extension : config.originalIpDetectionExtensions()) {
      const auto result = detection_extension->detect(params);

      if (result.reject_options.has_value()) {
        return {nullptr, result.reject_options};
      }

      if (result.detected_remote_address) {
        final_remote_address = result.detected_remote_address;
        allow_trusted_address_checks = result.allow_trusted_address_checks;
        break;
      }
    }
  }

  // If the x-forwarded-proto header is not set, set it here, since Envoy uses it for determining
  // scheme and communicating it upstream.
  if (!request_headers.ForwardedProto()) {
    request_headers.setReferenceForwardedProto(connection.ssl() ? Headers::get().SchemeValues.Https
                                                                : Headers::get().SchemeValues.Http);
  }
  // If :scheme is not set, sets :scheme based on X-Forwarded-Proto if a valid scheme,
  // else encryption level.
  // X-Forwarded-Proto and :scheme may still differ if different values are sent from downstream.
  if (!request_headers.Scheme() &&
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.add_and_validate_scheme_header")) {
    request_headers.setScheme(
        getScheme(request_headers.getForwardedProtoValue(), connection.ssl() != nullptr));
  }

  // At this point we can determine whether this is an internal or external request. The
  // determination of internal status uses the following:
  // 1) After remote address/XFF appending, the XFF header must contain a *single* address.
  // 2) The single address must be an internal address.
  // 3) If configured to not use remote address, but no XFF header is available, even if the real
  //    remote is internal, the request is considered external.
  // HUGE WARNING: The way we do this is not optimal but is how it worked "from the beginning" so
  //               we can't change it at this point. In the future we will likely need to add
  //               additional inference modes and make this mode legacy.
  const bool internal_request =
      allow_trusted_address_checks && final_remote_address != nullptr &&
      config.internalAddressConfig().isInternalAddress(*final_remote_address);

  // After determining internal request status, if there is no final remote address, due to no XFF,
  // busted XFF, etc., use the direct connection remote address for logging.
  if (final_remote_address == nullptr) {
    final_remote_address = connection.addressProvider().remoteAddress();
  }

  // Edge request is the request from external clients to front Envoy.
  // Request from front Envoy to the internal service will be treated as not edge request.
  const bool edge_request = !internal_request && config.useRemoteAddress();

  // If internal request, set header and do other internal only modifications.
  if (internal_request) {
    request_headers.setReferenceEnvoyInternalRequest(
        Headers::get().EnvoyInternalRequestValues.True);
  } else {
    cleanInternalHeaders(request_headers, edge_request, route_config.internalOnlyHeaders());
  }

  if (config.userAgent()) {
    request_headers.setEnvoyDownstreamServiceCluster(config.userAgent().value());
    const HeaderEntry* user_agent_header = request_headers.UserAgent();
    if (!user_agent_header || user_agent_header->value().empty()) {
      // Following setReference() is safe because user agent is constant for the life of the
      // listener.
      request_headers.setReferenceUserAgent(config.userAgent().value());
    }

    // TODO(htuch): should this be under the config.userAgent() condition or in the outer scope?
    if (!local_info.nodeName().empty()) {
      // Following setReference() is safe because local info is constant for the life of the server.
      request_headers.setReferenceEnvoyDownstreamServiceNode(local_info.nodeName());
    }
  }

  if (!config.via().empty()) {
    Utility::appendVia(request_headers, config.via());
  }

  // If we are an external request, AND we are "using remote address" (see above), we set
  // x-envoy-external-address since this is our first ingress point into the trusted network.
  if (edge_request && final_remote_address->type() == Network::Address::Type::Ip) {
    request_headers.setEnvoyExternalAddress(final_remote_address->ip()->addressAsString());
  }

  // Generate x-request-id for all edge requests, or if there is none.
  if (config.generateRequestId()) {
    auto rid_extension = config.requestIDExtension();
    // Unconditionally set a request ID if we are allowed to override it from
    // the edge. Otherwise just ensure it is set.
    const bool force_set = !config.preserveExternalRequestId() && edge_request;
    rid_extension->set(request_headers, force_set);
  }

  mutateXfccRequestHeader(request_headers, connection, config);

  return {final_remote_address, absl::nullopt};
}

void ConnectionManagerUtility::cleanInternalHeaders(
    RequestHeaderMap& request_headers, bool edge_request,
    const std::list<Http::LowerCaseString>& internal_only_headers) {
  if (edge_request) {
    request_headers.removeEnvoyDecoratorOperation();
    request_headers.removeEnvoyDownstreamServiceCluster();
    request_headers.removeEnvoyDownstreamServiceNode();
  }

  request_headers.removeEnvoyRetriableStatusCodes();
  request_headers.removeEnvoyRetriableHeaderNames();
  request_headers.removeEnvoyRetryOn();
  request_headers.removeEnvoyRetryGrpcOn();
  request_headers.removeEnvoyMaxRetries();
  request_headers.removeEnvoyUpstreamAltStatName();
  request_headers.removeEnvoyUpstreamRequestTimeoutMs();
  request_headers.removeEnvoyUpstreamRequestPerTryTimeoutMs();
  request_headers.removeEnvoyUpstreamRequestTimeoutAltResponse();
  request_headers.removeEnvoyExpectedRequestTimeoutMs();
  request_headers.removeEnvoyForceTrace();
  request_headers.removeEnvoyIpTags();
  request_headers.removeEnvoyOriginalUrl();
  request_headers.removeEnvoyHedgeOnPerTryTimeout();

  for (const LowerCaseString& header : internal_only_headers) {
    request_headers.remove(header);
  }
}

Tracing::Reason ConnectionManagerUtility::mutateTracingRequestHeader(
    RequestHeaderMap& request_headers, Runtime::Loader& runtime, ConnectionManagerConfig& config,
    const Router::Route* route) {
  Tracing::Reason final_reason = Tracing::Reason::NotTraceable;
  if (!config.tracingConfig()) {
    return final_reason;
  }

  auto rid_extension = config.requestIDExtension();
  const auto rid_to_integer = rid_extension->toInteger(request_headers);
  // Skip if request-id is corrupted, or non-existent
  if (!rid_to_integer.has_value()) {
    return final_reason;
  }
  const uint64_t result = rid_to_integer.value() % 10000;

  const envoy::type::v3::FractionalPercent* client_sampling =
      &config.tracingConfig()->client_sampling_;
  const envoy::type::v3::FractionalPercent* random_sampling =
      &config.tracingConfig()->random_sampling_;
  const envoy::type::v3::FractionalPercent* overall_sampling =
      &config.tracingConfig()->overall_sampling_;

  if (route && route->tracingConfig()) {
    client_sampling = &route->tracingConfig()->getClientSampling();
    random_sampling = &route->tracingConfig()->getRandomSampling();
    overall_sampling = &route->tracingConfig()->getOverallSampling();
  }

  // Do not apply tracing transformations if we are currently tracing.
  final_reason = rid_extension->getTraceReason(request_headers);
  if (Tracing::Reason::NotTraceable == final_reason) {
    if (request_headers.ClientTraceId() &&
        runtime.snapshot().featureEnabled("tracing.client_enabled", *client_sampling)) {
      final_reason = Tracing::Reason::ClientForced;
      rid_extension->setTraceReason(request_headers, final_reason);
    } else if (request_headers.EnvoyForceTrace()) {
      final_reason = Tracing::Reason::ServiceForced;
      rid_extension->setTraceReason(request_headers, final_reason);
    } else if (runtime.snapshot().featureEnabled("tracing.random_sampling", *random_sampling,
                                                 result)) {
      final_reason = Tracing::Reason::Sampling;
      rid_extension->setTraceReason(request_headers, final_reason);
    }
  }

  if (final_reason != Tracing::Reason::NotTraceable &&
      !runtime.snapshot().featureEnabled("tracing.global_enabled", *overall_sampling, result)) {
    final_reason = Tracing::Reason::NotTraceable;
    rid_extension->setTraceReason(request_headers, final_reason);
  }

  return final_reason;
}

void ConnectionManagerUtility::mutateXfccRequestHeader(RequestHeaderMap& request_headers,
                                                       Network::Connection& connection,
                                                       ConnectionManagerConfig& config) {
  // When AlwaysForwardOnly is set, always forward the XFCC header without modification.
  if (config.forwardClientCert() == ForwardClientCertType::AlwaysForwardOnly) {
    return;
  }
  // When Sanitize is set, or the connection is not mutual TLS, remove the XFCC header.
  if (config.forwardClientCert() == ForwardClientCertType::Sanitize ||
      !(connection.ssl() && connection.ssl()->peerCertificatePresented())) {
    request_headers.removeForwardedClientCert();
    return;
  }

  // When ForwardOnly is set, always forward the XFCC header without modification.
  if (config.forwardClientCert() == ForwardClientCertType::ForwardOnly) {
    return;
  }

  // TODO(myidpt): Handle the special characters in By and URI fields.
  // TODO: Optimize client_cert_details based on perf analysis (direct string appending may be more
  // preferable).
  std::vector<std::string> client_cert_details;
  // When AppendForward or SanitizeSet is set, the client certificate information should be set into
  // the XFCC header.
  if (config.forwardClientCert() == ForwardClientCertType::AppendForward ||
      config.forwardClientCert() == ForwardClientCertType::SanitizeSet) {
    const auto uri_sans_local_cert = connection.ssl()->uriSanLocalCertificate();
    if (!uri_sans_local_cert.empty()) {
      client_cert_details.push_back(absl::StrCat("By=", uri_sans_local_cert[0]));
    }
    const std::string cert_digest = connection.ssl()->sha256PeerCertificateDigest();
    if (!cert_digest.empty()) {
      client_cert_details.push_back(absl::StrCat("Hash=", cert_digest));
    }
    for (const auto& detail : config.setCurrentClientCertDetails()) {
      switch (detail) {
      case ClientCertDetailsType::Cert: {
        const std::string peer_cert = connection.ssl()->urlEncodedPemEncodedPeerCertificate();
        if (!peer_cert.empty()) {
          client_cert_details.push_back(absl::StrCat("Cert=\"", peer_cert, "\""));
        }
        break;
      }
      case ClientCertDetailsType::Chain: {
        const std::string peer_chain = connection.ssl()->urlEncodedPemEncodedPeerCertificateChain();
        if (!peer_chain.empty()) {
          client_cert_details.push_back(absl::StrCat("Chain=\"", peer_chain, "\""));
        }
        break;
      }
      case ClientCertDetailsType::Subject:
        // The "Subject" key still exists even if the subject is empty.
        client_cert_details.push_back(
            absl::StrCat("Subject=\"", connection.ssl()->subjectPeerCertificate(), "\""));
        break;
      case ClientCertDetailsType::URI: {
        // The "URI" key still exists even if the URI is empty.
        const auto sans = connection.ssl()->uriSanPeerCertificate();
        const auto& uri_san = sans.empty() ? "" : sans[0];
        client_cert_details.push_back(absl::StrCat("URI=", uri_san));
        break;
      }
      case ClientCertDetailsType::DNS: {
        auto dns_sans = connection.ssl()->dnsSansPeerCertificate();
        if (!dns_sans.empty()) {
          for (const std::string& dns : dns_sans) {
            client_cert_details.push_back(absl::StrCat("DNS=", dns));
          }
        }
        break;
      }
      }
    }
  }

  const std::string client_cert_details_str = absl::StrJoin(client_cert_details, ";");
  if (config.forwardClientCert() == ForwardClientCertType::AppendForward) {
    request_headers.appendForwardedClientCert(client_cert_details_str, ",");
  } else if (config.forwardClientCert() == ForwardClientCertType::SanitizeSet) {
    request_headers.setForwardedClientCert(client_cert_details_str);
  } else {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

void ConnectionManagerUtility::mutateResponseHeaders(ResponseHeaderMap& response_headers,
                                                     const RequestHeaderMap* request_headers,
                                                     ConnectionManagerConfig& config,
                                                     const std::string& via) {
  bool clear_hop_by_hop = config.clearHopByHopResponseHeaders();
  if (request_headers != nullptr && Utility::isUpgrade(*request_headers) &&
      Utility::isUpgrade(response_headers)) {
    // As in mutateRequestHeaders, Upgrade responses have special handling.
    //
    // Unlike mutateRequestHeaders there is no explicit protocol check. If Envoy is proxying an
    // upgrade response it has already passed the protocol checks.
    const bool no_body =
        (!response_headers.TransferEncoding() && !response_headers.ContentLength());

    const bool is_1xx = CodeUtility::is1xx(Utility::getResponseStatus(response_headers));

    // We are explicitly forbidden from setting content-length for 1xx responses
    // (RFC7230, Section 3.3.2). We ignore 204 because this is an upgrade.
    if (no_body && !is_1xx) {
      response_headers.setContentLength(uint64_t(0));
    }
  } else {
    if (clear_hop_by_hop) {
      response_headers.removeConnection();
      response_headers.removeUpgrade();
    }
  }

  if (request_headers != nullptr &&
      (config.alwaysSetRequestIdInResponse() || request_headers->EnvoyForceTrace())) {
    config.requestIDExtension()->setInResponse(response_headers, *request_headers);
  }
  if (clear_hop_by_hop) {
    response_headers.removeTransferEncoding();
    response_headers.removeKeepAlive();
    response_headers.removeProxyConnection();
  }
  if (!via.empty()) {
    Utility::appendVia(response_headers, via);
  }
}

ConnectionManagerUtility::NormalizePathAction
ConnectionManagerUtility::maybeNormalizePath(RequestHeaderMap& request_headers,
                                             const ConnectionManagerConfig& config) {
  if (!request_headers.Path()) {
    return NormalizePathAction::Continue; // It's as valid as it is going to get.
  }

  NormalizePathAction final_action = NormalizePathAction::Continue;
  const auto escaped_slashes_action = config.pathWithEscapedSlashesAction();
  ASSERT(escaped_slashes_action != envoy::extensions::filters::network::http_connection_manager::
                                       v3::HttpConnectionManager::IMPLEMENTATION_SPECIFIC_DEFAULT);
  if (escaped_slashes_action != envoy::extensions::filters::network::http_connection_manager::v3::
                                    HttpConnectionManager::KEEP_UNCHANGED) {
    auto escaped_slashes_result = PathUtil::unescapeSlashes(request_headers);
    if (escaped_slashes_result == PathUtil::UnescapeSlashesResult::FoundAndUnescaped) {
      if (escaped_slashes_action == envoy::extensions::filters::network::http_connection_manager::
                                        v3::HttpConnectionManager::REJECT_REQUEST) {
        return NormalizePathAction::Reject;
      } else if (escaped_slashes_action ==
                 envoy::extensions::filters::network::http_connection_manager::v3::
                     HttpConnectionManager::UNESCAPE_AND_REDIRECT) {
        final_action = NormalizePathAction::Redirect;
      } else {
        ASSERT(escaped_slashes_action ==
               envoy::extensions::filters::network::http_connection_manager::v3::
                   HttpConnectionManager::UNESCAPE_AND_FORWARD);
      }
    }
  }

  if (config.shouldNormalizePath() && !PathUtil::canonicalPath(request_headers)) {
    return NormalizePathAction::Reject;
  }

  // Merge slashes after path normalization to catch potential edge cases with percent encoding.
  if (config.shouldMergeSlashes()) {
    PathUtil::mergeSlashes(request_headers);
  }

  return final_action;
}

absl::optional<uint32_t>
ConnectionManagerUtility::maybeNormalizeHost(RequestHeaderMap& request_headers,
                                             const ConnectionManagerConfig& config, uint32_t port) {
  if (config.shouldStripTrailingHostDot()) {
    HeaderUtility::stripTrailingHostDot(request_headers);
  }
  if (config.stripPortType() == Http::StripPortType::Any) {
    return HeaderUtility::stripPortFromHost(request_headers, absl::nullopt);
  } else if (config.stripPortType() == Http::StripPortType::MatchingHost) {
    return HeaderUtility::stripPortFromHost(request_headers, port);
  }
  return absl::nullopt;
}

} // namespace Http
} // namespace Envoy
