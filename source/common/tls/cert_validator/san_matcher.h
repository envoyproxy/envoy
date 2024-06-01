#pragma once

#include <memory>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.pb.h"
#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/hash.h"
#include "source/common/common/matchers.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/tls/utility.h"

#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

/** Interface to verify if there is a match in a list of subject alternative
 * names.
 */
class SanMatcher {
public:
  virtual bool match(GENERAL_NAME const*) const PURE;
  virtual ~SanMatcher() = default;
};

using SanMatcherPtr = std::unique_ptr<SanMatcher>;

class StringSanMatcher : public SanMatcher {
public:
  bool match(const GENERAL_NAME* general_name) const override;
  ~StringSanMatcher() override = default;
  StringSanMatcher(int general_name_type, envoy::type::matcher::v3::StringMatcher matcher,
                   Server::Configuration::CommonFactoryContext& context,
                   bssl::UniquePtr<ASN1_OBJECT>&& general_name_oid = nullptr)
      : general_name_type_(general_name_type), matcher_(matcher, context),
        general_name_oid_(std::move(general_name_oid)) {}

private:
  const int general_name_type_;
  const Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher> matcher_;
  bssl::UniquePtr<ASN1_OBJECT> general_name_oid_;
};

SanMatcherPtr createStringSanMatcher(
    const envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher& matcher,
    Server::Configuration::CommonFactoryContext& context);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
