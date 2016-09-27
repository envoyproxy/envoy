#include "conn_manager_utility.h"

#include "common/common/empty_string.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/network/utility.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"

namespace Http {

void ConnectionManagerUtility::mutateRequestHeaders(Http::HeaderMap& request_headers,
                                                    Network::Connection& connection,
                                                    ConnectionManagerConfig& config,
                                                    Runtime::RandomGenerator& random,
                                                    Runtime::Loader& runtime) {
  // Clean proxy headers.
  request_headers.remove(Headers::get().Connection);
  request_headers.remove(Headers::get().EnvoyInternalRequest);
  request_headers.remove(Headers::get().KeepAlive);
  request_headers.remove(Headers::get().ProxyConnection);
  request_headers.remove(Headers::get().TransferEncoding);
  request_headers.remove(Headers::get().Upgrade);
  request_headers.remove(Headers::get().Version);

  // If we are "using remote address" this means that we create/append to XFF with our immediate
  // peer. Cases where we don't "use remote address" include trusted double proxy where we expect
  // our peer to have already properly set XFF, etc.
  if (config.useRemoteAddress()) {
    if (Network::Utility::isLoopbackAddress(connection.remoteAddress())) {
      Utility::appendXff(request_headers, config.localAddress());
    } else {
      Utility::appendXff(request_headers, connection.remoteAddress());
    }
    request_headers.replaceViaMoveValue(Headers::get().ForwardedProto,
                                        connection.ssl() ? "https" : "http");
  }

  // If we didn't already replace x-forwarded-proto because we are using the remote address, and
  // remote hasn't set it (trusted proxy), we set it, since we then use this for setting scheme.
  if (!request_headers.has(Headers::get().ForwardedProto)) {
    request_headers.addViaMoveValue(Headers::get().ForwardedProto,
                                    connection.ssl() ? "https" : "http");
  }

  request_headers.replaceViaCopy(Headers::get().Scheme,
                                 request_headers.get(Headers::get().ForwardedProto));

  // At this point we can determine whether this is an internal or external request. This is done
  // via XFF, which was set above or we trust.
  bool internal_request = Utility::isInternalRequest(request_headers);

  // Edge request is the request from external clients to front Envoy.
  // Request from front Envoy to the internal service will be treated as not edge request.
  bool edge_request = !internal_request && config.useRemoteAddress();

  // If internal request, set header and do other internal only modifications.
  if (internal_request) {
    request_headers.addViaMoveValue(Headers::get().EnvoyInternalRequest, "true");
  } else {
    if (edge_request) {
      request_headers.remove(Headers::get().EnvoyDownstreamServiceCluster);
    }

    request_headers.remove(Headers::get().EnvoyRetryOn);
    request_headers.remove(Headers::get().EnvoyUpstreamAltStatName);
    request_headers.remove(Headers::get().EnvoyUpstreamRequestTimeoutMs);
    request_headers.remove(Headers::get().EnvoyUpstreamRequestPerTryTimeoutMs);
    request_headers.remove(Headers::get().EnvoyExpectedRequestTimeoutMs);
    request_headers.remove(Headers::get().EnvoyForceTrace);

    for (const Http::LowerCaseString& header : config.routeConfig().internalOnlyHeaders()) {
      request_headers.remove(header);
    }
  }

  if (config.userAgent().valid()) {
    request_headers.replaceViaCopy(Headers::get().EnvoyDownstreamServiceCluster,
                                   config.userAgent().value());
    if (request_headers.get(Headers::get().UserAgent).empty()) {
      request_headers.replaceViaCopy(Headers::get().UserAgent, config.userAgent().value());
    }
  }

  // If we are an external request, AND we are "using remote address" (see above), we set
  // x-envoy-external-address since this is our first ingress point into the trusted network.
  if (edge_request) {
    request_headers.replaceViaCopy(Headers::get().EnvoyExternalAddress, connection.remoteAddress());
  }

  // Generate x-request-id for all edge requests, or if there is none.
  if (edge_request || request_headers.get(Headers::get().RequestId).empty()) {
    std::string uuid = "";

    try {
      uuid = random.uuid();
    } catch (const EnvoyException&) {
      // We could not generate uuid, not a big deal.
      config.stats().named_.failed_generate_uuid_.inc();
    }

    if (!uuid.empty()) {
      request_headers.replaceViaMoveValue(Headers::get().RequestId, std::move(uuid));
    }
  }

  Tracing::HttpTracerUtility::mutateHeaders(request_headers, runtime);
}

void ConnectionManagerUtility::mutateResponseHeaders(Http::HeaderMap& response_headers,
                                                     const Http::HeaderMap& request_headers,
                                                     ConnectionManagerConfig& config) {
  response_headers.remove(Headers::get().Connection);
  response_headers.remove(Headers::get().TransferEncoding);
  response_headers.remove(Headers::get().Version);

  for (const Http::LowerCaseString& to_remove : config.routeConfig().responseHeadersToRemove()) {
    response_headers.remove(to_remove);
  }

  for (const std::pair<Http::LowerCaseString, std::string>& to_add :
       config.routeConfig().responseHeadersToAdd()) {
    response_headers.addViaCopy(to_add.first, to_add.second);
  }

  if (request_headers.has(Headers::get().EnvoyForceTrace)) {
    response_headers.replaceViaCopy(Headers::get().RequestId,
                                    request_headers.get(Headers::get().RequestId));
  }
}

std::string
ConnectionManagerUtility::getLastAddressFromXFF(const Http::HeaderMap& request_headers) {
  std::vector<std::string> xff_address_list =
      StringUtil::split(request_headers.get(Headers::get().ForwardedFor), ',');

  if (xff_address_list.empty()) {
    return EMPTY_STRING;
  }
  return xff_address_list.back();
}

} // Http
