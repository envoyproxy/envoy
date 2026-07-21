#pragma once

#include <memory>

#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Rfc9440ClientCert {

class Rfc9440ClientCertFilterConfig {
public:
  Rfc9440ClientCertFilterConfig(bool set_client_cert_chain)
      : set_client_cert_chain_(set_client_cert_chain) {}

  bool setClientCertChain() const { return set_client_cert_chain_; }

private:
  const bool set_client_cert_chain_;
};

using Rfc9440ClientCertFilterConfigSharedPtr = std::shared_ptr<Rfc9440ClientCertFilterConfig>;

class Rfc9440ClientCertFilter : public Http::PassThroughDecoderFilter {
public:
  Rfc9440ClientCertFilter(Rfc9440ClientCertFilterConfigSharedPtr config)
      : config_(std::move(config)) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

private:
  Rfc9440ClientCertFilterConfigSharedPtr config_;
};

} // namespace Rfc9440ClientCert
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
