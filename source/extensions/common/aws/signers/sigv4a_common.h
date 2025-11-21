#pragma once

#include "source/common/singleton/const_singleton.h"
#include "source/extensions/common/aws/signer_base_impl.h"

using AwsSigningHeaderMatcherVector = std::vector<envoy::type::matcher::v3::StringMatcher>;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class SigV4ASignatureHeaderValues : public SignatureHeaderValues {
public:
  const Http::LowerCaseString RegionSet{"x-amz-region-set"};
};

using SigV4ASignatureHeaders = ConstSingleton<SigV4ASignatureHeaderValues>;

class SigV4ASignatureConstants : public SignatureConstants {
public:
  static constexpr absl::string_view SigV4AAuthorizationHeaderFormat =
      "AWS4-ECDSA-P256-SHA256 Credential={}, SignedHeaders={}, Signature={}";
  static constexpr absl::string_view SigV4ACredentialScopeFormat = "{}/{}/aws4_request";
  static constexpr absl::string_view SigV4ASignatureVersion = "AWS4A";
  static constexpr absl::string_view SigV4AStringToSignFormat = "{}\n{}\n{}\n{}";
  static constexpr absl::string_view SigV4AAlgorithm = "AWS4-ECDSA-P256-SHA256";
};

enum SigV4AKeyDerivationResult {
  AkdrSuccess,
  AkdrNextCounter,
  AkdrFailure,
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
