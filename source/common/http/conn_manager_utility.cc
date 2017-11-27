#include "common/http/conn_manager_utility.h"

#include <atomic>
#include <cstdint>
#include <string>

#include "common/access_log/access_log_formatter.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/network/utility.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Http {

void ConnectionManagerUtility::mutateRequestHeaders(
    Http::HeaderMap& request_headers, Protocol protocol, Network::Connection& connection,
    ConnectionManagerConfig& config, const Router::Config& route_config,
    Runtime::RandomGenerator& random, Runtime::Loader& runtime,
    const LocalInfo::LocalInfo& local_info) {
  // If this is a WebSocket Upgrade request, do not remove the Connection and Upgrade headers,
  // as we forward them verbatim to the upstream hosts.
  if (protocol == Protocol::Http11 && Utility::isWebSocketUpgradeRequest(request_headers)) {
    // The current WebSocket implementation re-uses the HTTP1 codec to send upgrade headers to
    // the upstream host. This adds the "transfer-encoding: chunked" request header if the stream
    // has not ended and content-length does not exist. In HTTP1.1, if transfer-encoding and
    // content-length both do not exist this means there is no request body. After transfer-encoding
    // is stripped here, the upstream request becomes invalid. We can fix it by explicitly adding a
    // "content-length: 0" request header here.
    const bool no_body = (!request_headers.TransferEncoding() && !request_headers.ContentLength());
    if (no_body) {
      request_headers.insertContentLength().value(uint64_t(0));
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
  if (config.useRemoteAddress()) {
    if (Network::Utility::isLoopbackAddress(connection.remoteAddress())) {
      Utility::appendXff(request_headers, config.localAddress());
    } else {
      Utility::appendXff(request_headers, connection.remoteAddress());
    }
    request_headers.insertForwardedProto().value().setReference(
        connection.ssl() ? Headers::get().SchemeValues.Https : Headers::get().SchemeValues.Http);
  }

  // If we didn't already replace x-forwarded-proto because we are using the remote address, and
  // remote hasn't set it (trusted proxy), we set it, since we then use this for setting scheme.
  if (!request_headers.ForwardedProto()) {
    request_headers.insertForwardedProto().value().setReference(
        connection.ssl() ? Headers::get().SchemeValues.Https : Headers::get().SchemeValues.Http);
  }

  // At this point we can determine whether this is an internal or external request. This is done
  // via XFF, which was set above or we trust.
  bool internal_request = Utility::isInternalRequest(request_headers);

  // Edge request is the request from external clients to front Envoy.
  // Request from front Envoy to the internal service will be treated as not edge request.
  bool edge_request = !internal_request && config.useRemoteAddress();

  // If internal request, set header and do other internal only modifications.
  if (internal_request) {
    request_headers.insertEnvoyInternalRequest().value().setReference(
        Headers::get().EnvoyInternalRequestValues.True);
  } else {
    if (edge_request) {
      request_headers.removeEnvoyDecoratorOperation();
      request_headers.removeEnvoyDownstreamServiceCluster();
      request_headers.removeEnvoyDownstreamServiceNode();
    }

    request_headers.removeEnvoyRetryOn();
    request_headers.removeEnvoyRetryGrpcOn();
    request_headers.removeEnvoyMaxRetries();
    request_headers.removeEnvoyUpstreamAltStatName();
    request_headers.removeEnvoyUpstreamRequestTimeoutMs();
    request_headers.removeEnvoyUpstreamRequestPerTryTimeoutMs();
    request_headers.removeEnvoyUpstreamRequestTimeoutAltResponse();
    request_headers.removeEnvoyExpectedRequestTimeoutMs();
    request_headers.removeEnvoyForceTrace();

    for (const Http::LowerCaseString& header : route_config.internalOnlyHeaders()) {
      request_headers.remove(header);
    }
  }

  if (config.userAgent().valid()) {
    request_headers.insertEnvoyDownstreamServiceCluster().value(config.userAgent().value());
    HeaderEntry& user_agent_header = request_headers.insertUserAgent();
    if (user_agent_header.value().empty()) {
      // Following setReference() is safe because user agent is constant for the life of the
      // listener.
      user_agent_header.value().setReference(config.userAgent().value());
    }

    if (!local_info.nodeName().empty()) {
      request_headers.insertEnvoyDownstreamServiceNode().value(local_info.nodeName());
    }
  }

  // If we are an external request, AND we are "using remote address" (see above), we set
  // x-envoy-external-address since this is our first ingress point into the trusted network.
  if (edge_request && connection.remoteAddress().type() == Network::Address::Type::Ip) {
    request_headers.insertEnvoyExternalAddress().value(
        connection.remoteAddress().ip()->addressAsString());
  }

  // Generate x-request-id for all edge requests, or if there is none.
  if (config.generateRequestId() && (edge_request || !request_headers.RequestId())) {
    // TODO(PiotrSikora) PERF: Write UUID directly to the header map.
    const std::string uuid = random.uuid();
    ASSERT(!uuid.empty());
    request_headers.insertRequestId().value(uuid);
  }

  if (config.tracingConfig()) {
    Tracing::HttpTracerUtility::mutateHeaders(request_headers, runtime);
  }
  mutateXfccRequestHeader(request_headers, connection, config);
}

void ConnectionManagerUtility::mutateXfccRequestHeader(Http::HeaderMap& request_headers,
                                                       Network::Connection& connection,
                                                       ConnectionManagerConfig& config) {
  // When AlwaysForwardOnly is set, always forward the XFCC header without modification.
  if (config.forwardClientCert() == Http::ForwardClientCertType::AlwaysForwardOnly) {
    return;
  }
  // When Sanitize is set, or the connection is not mutual TLS, remove the XFCC header.
  if (config.forwardClientCert() == Http::ForwardClientCertType::Sanitize ||
      !(connection.ssl() && connection.ssl()->peerCertificatePresented())) {
    request_headers.removeForwardedClientCert();
    return;
  }

  // When ForwardOnly is set, always forward the XFCC header without modification.
  if (config.forwardClientCert() == Http::ForwardClientCertType::ForwardOnly) {
    return;
  }

  // TODO(myidpt): Handle the special characters in By and SAN fields.
  // TODO: Optimize client_cert_details based on perf analysis (direct string appending may be more
  // preferable).
  std::vector<std::string> client_cert_details;
  // When AppendForward or SanitizeSet is set, the client certificate information should be set into
  // the XFCC header.
  if (config.forwardClientCert() == Http::ForwardClientCertType::AppendForward ||
      config.forwardClientCert() == Http::ForwardClientCertType::SanitizeSet) {
    if (!connection.ssl()->uriSanLocalCertificate().empty()) {
      client_cert_details.push_back("By=" + connection.ssl()->uriSanLocalCertificate());
    }
    if (!connection.ssl()->sha256PeerCertificateDigest().empty()) {
      client_cert_details.push_back("Hash=" + connection.ssl()->sha256PeerCertificateDigest());
    }
    for (const auto& detail : config.setCurrentClientCertDetails()) {
      switch (detail) {
      case Http::ClientCertDetailsType::Subject:
        // The "Subject" key still exists even if the subject is empty.
        client_cert_details.push_back("Subject=\"" + connection.ssl()->subjectPeerCertificate() +
                                      "\"");
        break;
      case Http::ClientCertDetailsType::SAN:
        // Currently, we only support a single SAN field with URI type.
        // The "SAN" key still exists even if the SAN is empty.
        client_cert_details.push_back("SAN=" + connection.ssl()->uriSanPeerCertificate());
      }
    }
  }

  std::string client_cert_details_str = StringUtil::join(client_cert_details, ";");
  if (config.forwardClientCert() == Http::ForwardClientCertType::AppendForward) {
    if (request_headers.ForwardedClientCert() &&
        !request_headers.ForwardedClientCert()->value().empty()) {
      request_headers.ForwardedClientCert()->value().append(("," + client_cert_details_str).c_str(),
                                                            client_cert_details_str.length() + 1);
    } else {
      request_headers.insertForwardedClientCert().value(client_cert_details_str);
    }
  } else if (config.forwardClientCert() == Http::ForwardClientCertType::SanitizeSet) {
    request_headers.insertForwardedClientCert().value(client_cert_details_str);
  } else {
    NOT_REACHED;
  }
}

void ConnectionManagerUtility::mutateResponseHeaders(Http::HeaderMap& response_headers,
                                                     const Http::HeaderMap& request_headers) {
  response_headers.removeConnection();
  response_headers.removeTransferEncoding();

  if (request_headers.EnvoyForceTrace() && request_headers.RequestId()) {
    response_headers.insertRequestId().value(*request_headers.RequestId());
  }
}

} // namespace Http
} // namespace Envoy
