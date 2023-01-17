#include "source/extensions/filters/http/credentials/source_bearer_token.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Credentials {

BearerTokenCredentialSource::BearerTokenCredentialSource(
  ThreadLocal::SlotAllocator& tls, Api::Api& api, Secret::GenericSecretConfigProviderSharedPtr token_secret) :
  tls_(tls.allocateSlot()),
  token_reader_(std::make_unique<SDSSecretReader>(token_secret, api, *this)) {
  ThreadLocalBearerTokenCredentialSourceSharedPtr empty(new ThreadLocalBearerTokenCredentialSource(""));
  tls_->set(
    [empty](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr { return empty; });

  onSecretUpdate();
}

void BearerTokenCredentialSource::onSecretUpdate() {
  if (!token_reader_) {
      return;
  }
  const auto& token = StringUtil::trim(token_reader_->value());
  if (token.empty()) {
      return;
  }

  const auto authz_header = absl::StrCat("Bearer ", token);

  ThreadLocalBearerTokenCredentialSourceSharedPtr value(new ThreadLocalBearerTokenCredentialSource(authz_header));
  tls_->set([value](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return value;
  });
}

} // namespace Credentials
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
