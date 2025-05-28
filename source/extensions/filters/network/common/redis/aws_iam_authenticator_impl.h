#pragma once

#include <string>

#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/common/aws/signer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace AwsIamAuthenticator {

class AwsIamAuthenticatorBase : public Logger::Loggable<Logger::Id::aws> {
public:
  virtual ~AwsIamAuthenticatorBase() = default;
  virtual std::string getAuthToken(std::string auth_user) PURE;
  virtual bool
  addCallbackIfCredentialsPending(Extensions::Common::Aws::CredentialsPendingCallback&& cb) PURE;
};

class AwsIamAuthenticatorImpl : public AwsIamAuthenticatorBase {
public:
  AwsIamAuthenticatorImpl(Server::Configuration::ServerFactoryContext& context,
                          absl::string_view cache_name, absl::string_view service_name,
                          absl::string_view region, uint16_t expiration_time,
                          absl::optional<envoy::extensions::common::aws::v3::AwsCredentialProvider>
                              credential_provider);
  bool addCallbackIfCredentialsPending(
      Extensions::Common::Aws::CredentialsPendingCallback&& cb) override {
    return signer_->addCallbackIfCredentialsPending(std::move(cb));
  };

  std::string getAuthToken(std::string auth_user) override;

  using AwsIamAuthenticatorSharedPtr = std::shared_ptr<AwsIamAuthenticatorBase>;

private:
  Envoy::Extensions::Common::Aws::SignerPtr signer_;
  uint16_t expiration_time_;
  const std::string auth_user_;
  std::string cache_name_;
  std::string service_name_;
  std::string region_;
  Server::Configuration::ServerFactoryContext& context_;
  std::string auth_token_;
};

using AwsIamAuthenticatorSharedPtr = std::shared_ptr<AwsIamAuthenticatorBase>;
using AwsIamAuthenticatorSharedPtrOptRef = OptRef<AwsIamAuthenticatorSharedPtr>;

class AwsIamAuthenticatorFactory {
public:
  static AwsIamAuthenticatorSharedPtr initAwsIamAuthenticator(
      Server::Configuration::ServerFactoryContext& context,
      envoy::extensions::filters::network::redis_proxy::v3::AwsIam aws_iam_config);
};

} // namespace AwsIamAuthenticator
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
