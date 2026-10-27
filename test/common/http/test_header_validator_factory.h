#pragma once

#include <memory>

#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/http/header_validator.h"
#include "envoy/http/protocol.h"

#include "source/extensions/http/header_validators/envoy_default/http1_header_validator.h"
#include "source/extensions/http/header_validators/envoy_default/http2_header_validator.h"

namespace Envoy {
namespace Http {

// A HeaderValidatorFactory for tests that creates the default (envoy_default) server header
// validators from a test supplied configuration. Codec tests hand this factory to server codecs
// to exercise the UHV code path (when built with ENVOY_ENABLE_UHV).
//
// The config and overrides are captured by reference and may be mutated by the test before the
// first validator is created (codecs create validators lazily, per request).
class TestHeaderValidatorFactory : public HeaderValidatorFactory {
public:
  TestHeaderValidatorFactory(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      const Extensions::Http::HeaderValidators::EnvoyDefault::ConfigOverrides& config_overrides)
      : config_(config), config_overrides_(config_overrides) {}

  ServerHeaderValidatorPtr createServerHeaderValidator(Protocol protocol,
                                                       HeaderValidatorStats& stats) override {
    switch (protocol) {
    case Protocol::Http3:
    case Protocol::Http2:
      return std::make_unique<
          Extensions::Http::HeaderValidators::EnvoyDefault::ServerHttp2HeaderValidator>(
          config_, protocol, stats, config_overrides_);
    case Protocol::Http11:
    case Protocol::Http10:
      return std::make_unique<
          Extensions::Http::HeaderValidators::EnvoyDefault::ServerHttp1HeaderValidator>(
          config_, protocol, stats, config_overrides_);
    }
    return nullptr;
  }

  // Client side validation is not exercised by server codec tests.
  ClientHeaderValidatorPtr createClientHeaderValidator(Protocol, HeaderValidatorStats&) override {
    return nullptr;
  }

private:
  const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
      config_;
  const Extensions::Http::HeaderValidators::EnvoyDefault::ConfigOverrides& config_overrides_;
};

} // namespace Http
} // namespace Envoy
