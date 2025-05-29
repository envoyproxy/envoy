#pragma once

#include <string>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/common/aws/signer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace AwsIamAuthenticator {

namespace {
static constexpr uint16_t AwsIamDefaultExpiration = 60;
constexpr char DEFAULT_SERVICE_NAME[] = "elasticache";
} // namespace

class AwsIamAuthenticatorBase : public Logger::Loggable<Logger::Id::aws>, public Singleton::Instance {
public:
  ~AwsIamAuthenticatorBase() override = default;
  virtual std::string getAuthToken(std::string auth_user) PURE;
  virtual bool
  addCallbackIfCredentialsPending(Extensions::Common::Aws::CredentialsPendingCallback&& cb) PURE;
};

class AwsIamAuthenticatorImpl : public AwsIamAuthenticatorBase {
public:
  AwsIamAuthenticatorImpl(absl::string_view cache_name, absl::string_view region,
                          Envoy::Extensions::Common::Aws::SignerPtr signer);

  bool addCallbackIfCredentialsPending(
      Extensions::Common::Aws::CredentialsPendingCallback&& cb) override {
    return signer_->addCallbackIfCredentialsPending(std::move(cb));
  };

  std::string getAuthToken(std::string auth_user) override;

  using AwsIamAuthenticatorSharedPtr = std::shared_ptr<AwsIamAuthenticatorBase>;

private:
  Envoy::Extensions::Common::Aws::SignerPtr signer_;
  const std::string auth_user_;
  std::string cache_name_;
  std::string auth_token_;
  std::string region_;
};

using AwsIamAuthenticatorSharedPtr = std::shared_ptr<AwsIamAuthenticatorBase>;
using AwsIamAuthenticatorSharedPtrOptRef = OptRef<AwsIamAuthenticatorSharedPtr>;

class AwsIamAuthenticatorFactory : public Logger::Loggable<Logger::Id::aws> {
public:
  static absl::optional<AwsIamAuthenticatorSharedPtr> initAwsIamAuthenticator(
      Server::Configuration::ServerFactoryContext& context,
      envoy::extensions::filters::network::redis_proxy::v3::AwsIam aws_iam_config);
};

} // namespace AwsIamAuthenticator
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
