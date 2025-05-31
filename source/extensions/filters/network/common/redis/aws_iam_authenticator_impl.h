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

class AwsIamAuthenticatorBase : public Logger::Loggable<Logger::Id::aws> {
public:
  virtual ~AwsIamAuthenticatorBase() = default;
  virtual std::string getAuthToken(absl::string_view auth_user, absl::string_view cache_name) PURE;
  virtual bool
  addCallbackIfCredentialsPending(Extensions::Common::Aws::CredentialsPendingCallback&& cb) PURE;

};

class AwsIamAuthenticatorImpl : public AwsIamAuthenticatorBase {
public:
  AwsIamAuthenticatorImpl(Envoy::Extensions::Common::Aws::SignerPtr signer);
  ~AwsIamAuthenticatorImpl() override {
    signer_.reset();
  }
  
  bool addCallbackIfCredentialsPending(
      Extensions::Common::Aws::CredentialsPendingCallback&& cb) override {
    return signer_->addCallbackIfCredentialsPending(std::move(cb));
  };

  std::string getAuthToken(absl::string_view auth_user, absl::string_view cache_name) override;

private:
  Envoy::Extensions::Common::Aws::SignerPtr signer_;
  std::string auth_token_;
  std::string region_;

};

using AwsIamAuthenticatorSharedPtr = std::shared_ptr<AwsIamAuthenticatorImpl>;

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
