#pragma once

#include <memory>

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "contrib/envoy/extensions/filters/http/client_cert/v3alpha/client_cert.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ClientCert {

class ClientCertFilterConfig {
public:
  ClientCertFilterConfig(
      const envoy::extensions::filters::http::client_cert::v3alpha::ClientCertConfig& proto_config)
      : set_client_cert_chain_(proto_config.set_client_cert_chain()) {}

  bool setClientCertChain() const { return set_client_cert_chain_; }

private:
  const bool set_client_cert_chain_;
};

using ClientCertFilterConfigSharedPtr = std::shared_ptr<ClientCertFilterConfig>;

/**
 * A filter that forwards the downstream mTLS client certificate to the upstream using the
 * RFC 9440 Client-Cert and Client-Cert-Chain headers. Incoming occurrences of these headers
 * are always removed, as required by RFC 9440 section 2.4.
 */
class ClientCertFilter : public Http::PassThroughDecoderFilter {
public:
  ClientCertFilter(ClientCertFilterConfigSharedPtr config) : config_(std::move(config)) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

private:
  const ClientCertFilterConfigSharedPtr config_;
};

} // namespace ClientCert
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
