#pragma once

#include "source/common/secret/secret_provider_impl.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace Common {
// Helper class used to fetch secrets (usually from SDS).
class SecretReader {
public:
  virtual ~SecretReader() = default;
  virtual const std::string& credential() const PURE;
};

using SecretReaderConstSharedPtr = std::shared_ptr<const SecretReader>;

class SDSSecretReader : public SecretReader {
public:
  SDSSecretReader(Secret::GenericSecretConfigProviderSharedPtr secret_provider,
                  ThreadLocal::SlotAllocator& tls, Api::Api& api)
      : credential_(THROW_OR_RETURN_VALUE(
            Secret::ThreadLocalGenericSecretProvider::create(std::move(secret_provider), tls, api),
            Secret::ThreadLocalGenericSecretProvider)) {}
  const std::string& credential() const override { return credential_.secret(); }

private:
  Secret::ThreadLocalGenericSecretProvider credential_;
};

} // namespace Common
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
