#include "source/extensions/filters/http/credentials/source_basic_auth.h"

#include "source/common/common/base64.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Credentials {

BasicAuthCredentialSource::BasicAuthCredentialSource(
    ThreadLocal::SlotAllocator& tls,
    Api::Api& api,
    Secret::GenericSecretConfigProviderSharedPtr username_secret,
    Secret::GenericSecretConfigProviderSharedPtr password_secret) :
    tls_(tls.allocateSlot()),
    username_reader_(std::make_unique<SDSSecretReader>(username_secret, api, *this)),
    password_reader_(std::make_unique<SDSSecretReader>(password_secret, api, *this)) {
  ThreadLocalBasicAuthCredentialSourceSharedPtr empty(new ThreadLocalBasicAuthCredentialSource(""));
  tls_->set(
    [empty](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr { return empty; });

  onSecretUpdate();
}

void BasicAuthCredentialSource::onSecretUpdate() {
  if (!username_reader_) {
    return;
  }
  const auto& username = StringUtil::trim(username_reader_->value());
  if (username.empty()) {
    return;
  }
  if (!password_reader_) {
    return;
  }
  const auto& password = StringUtil::trim(password_reader_->value());
  if (password.empty()) {
    return;
  }

  const auto authz = absl::StrCat(username, ":", password);
  const auto authz_base64 = Envoy::Base64::encode(authz.data(), authz.length());
  const auto authz_header = absl::StrCat("Basic ", authz_base64);

  ThreadLocalBasicAuthCredentialSourceSharedPtr value(new ThreadLocalBasicAuthCredentialSource(authz_header));
  tls_->set([value](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return value;
  });
}

} // namespace Credentials
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
