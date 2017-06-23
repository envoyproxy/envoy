#include "common/http/conn_manager_utility.h"

#include <atomic>
#include <cstdint>
#include <string>

#include "common/common/empty_string.h"
#include "common/http/access_log/access_log_formatter.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/network/utility.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Http {

std::atomic<uint64_t> ConnectionManagerUtility::next_stream_id_(0);

uint64_t ConnectionManagerUtility::generateStreamId(const Router::Config& route_table,
                                                    Runtime::RandomGenerator& random_generator) {
  // See the comment for next_stream_id_ in conn_manager_utility.h for why we do this.
  if (route_table.usesRuntime()) {
    return random_generator.random();
  } else {
    return ++next_stream_id_;
  }
}

void ConnectionManagerUtility::mutateRequestHeaders(Http::HeaderMap& request_headers,
                                                    Network::Connection& connection,
                                                    ConnectionManagerConfig& config,
                                                    const Router::Config& route_config,
                                                    Runtime::RandomGenerator& random,
                                                    Runtime::Loader& runtime,
                                                    const LocalInfo::LocalInfo& local_info) {
  // Clean proxy headers.
  request_headers.removeConnection();
  request_headers.removeEnvoyInternalRequest();
  request_headers.removeKeepAlive();
  request_headers.removeProxyConnection();
  request_headers.removeTransferEncoding();
  request_headers.removeUpgrade();

  // If we are "using remote address" this means that we create/append to XFF with our immediate
  // peer. Cases where we don't "use remote address" include trusted double proxy where we expect
  // our peer to have already properly set XFF, etc.
  if (config.useRemoteAddress()) {
    if (Network::Utility::isLoopbackAddress(connection.remoteAddress())) {
      Utility::appendXff(request_headers, config.localAddress());
    } else {
      Utility::appendXff(request_headers, connection.remoteAddress());
    }
    request_headers.insertForwardedProto().value(
        connection.ssl() ? Headers::get().SchemeValues.Https : Headers::get().SchemeValues.Http);
  }

  // If we didn't already replace x-forwarded-proto because we are using the remote address, and
  // remote hasn't set it (trusted proxy), we set it, since we then use this for setting scheme.
  if (!request_headers.ForwardedProto()) {
    request_headers.insertForwardedProto().value(
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
    request_headers.insertEnvoyInternalRequest().value(
        Headers::get().EnvoyInternalRequestValues.True);
  } else {
    if (edge_request) {
      request_headers.removeEnvoyDownstreamServiceCluster();
      request_headers.removeEnvoyDownstreamServiceNode();
    }

    request_headers.removeEnvoyRetryOn();
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
      user_agent_header.value(config.userAgent().value());
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
    std::string uuid = "";

    try {
      uuid = random.uuid();
    } catch (const EnvoyException&) {
      // We could not generate uuid, not a big deal.
      config.stats().named_.failed_generate_uuid_.inc();
    }

    if (!uuid.empty()) {
      request_headers.insertRequestId().value(uuid);
    }
  }

  if (config.tracingConfig()) {
    Tracing::HttpTracerUtility::mutateHeaders(request_headers, runtime);
  }

  if ((connection.ssl() && connection.ssl()->peerCertificatePresented()) ||
      config.forwardClientCert() == Http::ForwardClientCertType::AlwaysForwardOnly) {
    std::string clientCertDetails;
    if (config.forwardClientCert() == Http::ForwardClientCertType::AppendForward ||
        config.forwardClientCert() == Http::ForwardClientCertType::SanitizeSet) {
      // In these cases, the client cert in the current hop should be set into the XFCC header.
      if (!connection.ssl()->uriSanLocalCertificate().empty()) {
        clientCertDetails += "By=" + connection.ssl()->uriSanLocalCertificate();
      }
      if (!connection.ssl()->sha256PeerCertificateDigest().empty()) {
        clientCertDetails += ";Hash=" + connection.ssl()->sha256PeerCertificateDigest();
      }
      for (auto detail : config.setCurrentClientCertDetails()) {
        if (detail == Http::ClientCertDetailsType::Subject) {
          clientCertDetails += ";Subject=\"" + connection.ssl()->subjectPeerCertificate() + "\"";
        } else if (detail == Http::ClientCertDetailsType::SAN) {
          // Currently, we only support a single SAN field with URI type.
          clientCertDetails += ";SAN=" + connection.ssl()->uriSanPeerCertificate();
        }
      }
    }

    switch (config.forwardClientCert()) {
    case Http::ForwardClientCertType::ForwardOnly:
    case Http::ForwardClientCertType::AlwaysForwardOnly:
      break;
    case Http::ForwardClientCertType::AppendForward:
      // Get the Cert info.
      if (request_headers.ForwardedClientCert() &&
          !request_headers.ForwardedClientCert()->value().empty()) {
        request_headers.ForwardedClientCert()->value().append(("," + clientCertDetails).c_str(),
                                                              clientCertDetails.length() + 1);
      } else {
        request_headers.insertForwardedClientCert().value(clientCertDetails);
      }
      break;
    case Http::ForwardClientCertType::Sanitize:
      request_headers.removeForwardedClientCert();
      break;
    case Http::ForwardClientCertType::SanitizeSet:
      request_headers.insertForwardedClientCert().value(clientCertDetails);
    }
  } else {
    request_headers.removeForwardedClientCert();
  }
}

void ConnectionManagerUtility::mutateResponseHeaders(Http::HeaderMap& response_headers,
                                                     const Http::HeaderMap& request_headers,
                                                     const Router::Config& route_config) {
  response_headers.removeConnection();
  response_headers.removeTransferEncoding();

  for (const Http::LowerCaseString& to_remove : route_config.responseHeadersToRemove()) {
    response_headers.remove(to_remove);
  }

  for (const std::pair<Http::LowerCaseString, std::string>& to_add :
       route_config.responseHeadersToAdd()) {
    response_headers.addStatic(to_add.first, to_add.second);
  }

  if (request_headers.EnvoyForceTrace() && request_headers.RequestId()) {
    response_headers.insertRequestId().value(*request_headers.RequestId());
  }
}

} // Http
} // Envoy
