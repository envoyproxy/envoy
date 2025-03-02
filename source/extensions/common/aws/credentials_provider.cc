#include "source/extensions/common/aws/credentials_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

Credentials CredentialsProviderChain::chainGetCredentials() {
  for (auto& provider : providers_) {
    const auto credentials = provider->getCredentials();
    if (credentials.hasCredentials()) {
      return credentials;
    }
  }

  ENVOY_LOG(debug, "No AWS credentials found, using anonymous credentials");
  return Credentials();
}

void CredentialsProviderChain::storeSubscription(
    CredentialSubscriberCallbacksHandlePtr subscription) {
  subscriber_handles_.push_back(std::move(subscription));
}

// Determine if we have a provider that is pending, based on priority ordering in the chain.
// Ignore any non-pending providers that have no credentials for us.

bool CredentialsProviderChain::chainProvidersPending() {
  for (auto& provider : providers_) {
    if (provider->credentialsPending()) {
      ENVOY_LOG(debug, "Provider {} is still pending", provider->providerName());
      return true;
    }
    if (provider->getCredentials().hasCredentials()) {
      ENVOY_LOG(debug, "Provider {} has credentials", provider->providerName());
      break;
    } else {
      ENVOY_LOG(debug, "Provider {} has blank credentials, continuing through chain",
                provider->providerName());
    }
  }
  return false;
}

bool CredentialsProviderChain::addCallbackIfChainCredentialsPending(
    CredentialsPendingCallback&& cb) {
  if (!chainProvidersPending()) {
    return false;
  }
  if (cb) {
    ENVOY_LOG(debug, "Adding credentials pending callback to queue");
    Thread::LockGuard guard(mu_);
    credential_pending_callbacks_.push_back(std::move(cb));
    ENVOY_LOG(debug, "We have {} pending callbacks", credential_pending_callbacks_.size());
  }
  return true;
}

void CredentialsProviderChain::onCredentialUpdate() {
  if (chainProvidersPending()) {
    return;
  }

  std::vector<CredentialsPendingCallback> callbacks_copy;

  {
    Thread::LockGuard guard(mu_);
    callbacks_copy = credential_pending_callbacks_;
    credential_pending_callbacks_.clear();
  }

  ENVOY_LOG(debug, "Notifying {} credential callbacks", callbacks_copy.size());

  // Call all of our callbacks to unblock pending requests
  for (const auto& cb : callbacks_copy) {
    cb();
  }
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
