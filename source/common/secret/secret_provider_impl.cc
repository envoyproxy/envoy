#include "source/common/secret/secret_provider_impl.h"

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/config/datasource.h"
#include "source/common/ssl/certificate_validation_context_config_impl.h"
#include "source/common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

absl::StatusOr<std::unique_ptr<ThreadLocalGenericSecretProvider>>
ThreadLocalGenericSecretProvider::create(GenericSecretConfigProviderSharedPtr&& provider,
                                         ThreadLocal::SlotAllocator& tls, Api::Api& api) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<ThreadLocalGenericSecretProvider>(
      new ThreadLocalGenericSecretProvider(std::move(provider), tls, api, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}
ThreadLocalGenericSecretProvider::ThreadLocalGenericSecretProvider(
    GenericSecretConfigProviderSharedPtr&& provider, ThreadLocal::SlotAllocator& tls, Api::Api& api,
    absl::Status& creation_status)
    : provider_(provider), api_(api),
      tls_(std::make_unique<ThreadLocal::TypedSlot<ThreadLocalSecret>>(tls)),
      cb_(provider_->addUpdateCallback([this] { return update(); })) {
  std::string value;
  absl::flat_hash_map<std::string, std::string> values;
  SET_AND_RETURN_IF_NOT_OK(read(value, values), creation_status);
  tls_->set([value = std::move(value), values = std::move(values)](Event::Dispatcher&) {
    return std::make_shared<ThreadLocalSecret>(value, values);
  });
}

const std::string& ThreadLocalGenericSecretProvider::secret() const { return (*tls_)->value_; }

const std::string& ThreadLocalGenericSecretProvider::secret(absl::string_view name) const {
  const auto& values = (*tls_)->values_;
  const auto it = values.find(name);
  return it != values.end() ? it->second : EMPTY_STRING;
}

absl::Status ThreadLocalGenericSecretProvider::read(
    std::string& value, absl::flat_hash_map<std::string, std::string>& values) const {
  const auto* secret = provider_->secret();
  if (secret == nullptr) {
    return absl::OkStatus();
  }
  auto value_or_error = Config::DataSource::read(secret->secret(), true, api_);
  RETURN_IF_NOT_OK_REF(value_or_error.status());
  value = std::move(value_or_error.value());
  for (const auto& [name, source] : secret->secrets()) {
    auto entry_or_error = Config::DataSource::read(source, true, api_);
    RETURN_IF_NOT_OK_REF(entry_or_error.status());
    values.emplace(name, std::move(entry_or_error.value()));
  }
  return absl::OkStatus();
}

// This function is executed on the main during xDS update.
absl::Status ThreadLocalGenericSecretProvider::update() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  std::string value;
  absl::flat_hash_map<std::string, std::string> values;
  RETURN_IF_NOT_OK(read(value, values));
  tls_->set([value = std::move(value), values = std::move(values)](Event::Dispatcher&) {
    return std::make_shared<ThreadLocalSecret>(value, values);
  });
  return absl::OkStatus();
}

} // namespace Secret
} // namespace Envoy
