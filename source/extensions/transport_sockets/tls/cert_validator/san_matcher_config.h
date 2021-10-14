#pragma once

#include <memory>

#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/matchers.h"
#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"
#include "source/extensions/transport_sockets/tls/utility.h"

#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

template <int SanTypeId> class DefaultSanMatcher : public Envoy::Ssl::SanMatcher {
public:
  bool match(const GENERAL_NAMES* general_names) const override {
    for (const GENERAL_NAME* general_name : general_names) {
      if (general_name->type == SanTypeId &&
          DefaultCertValidator::verifySubjectAltName(general_name, matcher_)) {
        return true;
      }
    }
    return false;
  }

  ~DefaultSanMatcher() override = default;

  DefaultSanMatcher(envoy::type::matcher::v3::StringMatcher matcher) : matcher_(matcher) {}

private:
  Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher> matcher_;
};

using DnsSanMatcher = DefaultSanMatcher<GEN_DNS>;
using EmailSanMatcher = DefaultSanMatcher<GEN_EMAIL>;
using UriSanMatcher = DefaultSanMatcher<GEN_URI>;
using IpAddSanMatcher = DefaultSanMatcher<GEN_IPADD>;

class DnsSanMatcherFactory : public Envoy::Ssl::SanMatcherFactory {
public:
  ~DnsSanMatcherFactory() override = default;
  Envoy::Ssl::SanMatcherPtr createSanMatcher(const Protobuf::Message* config) override;
  std::string name() const override { return "envoy.san_matchers.dns_san_matcher"; }
};

class EmailSanMatcherFactory : public Envoy::Ssl::SanMatcherFactory {
public:
  ~EmailSanMatcherFactory() override = default;
  Envoy::Ssl::SanMatcherPtr createSanMatcher(const Protobuf::Message* config) override;
  std::string name() const override { return "envoy.san_matchers.email_san_matcher"; }
};

class UriSanMatcherFactory : public Envoy::Ssl::SanMatcherFactory {
public:
  ~UriSanMatcherFactory() override = default;
  Envoy::Ssl::SanMatcherPtr createSanMatcher(const Protobuf::Message* config) override;
  std::string name() const override { return "envoy.san_matchers.uri_san_matcher"; }
};

class IpAddSanMatcherFactory : public Envoy::Ssl::SanMatcherFactory {
public:
  ~IpAddSanMatcherFactory() override = default;
  Envoy::Ssl::SanMatcherPtr createSanMatcher(const Protobuf::Message* config) override;
  std::string name() const override { return "envoy.san_matchers.ip_add_san_matcher"; }
};

class BackwardsCompatibleSanMatcher : public Envoy::Ssl::SanMatcher {

public:
  bool match(const GENERAL_NAMES* general_names) const override;

  ~BackwardsCompatibleSanMatcher() override = default;

  BackwardsCompatibleSanMatcher(envoy::type::matcher::v3::StringMatcher matcher)
      : matcher_(matcher) {}

private:
  Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher> matcher_;
};

Envoy::Ssl ::SanMatcherPtr createBackwardsCompatibleSanMatcher(
    envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher const& matcher);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
