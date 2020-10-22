#include "extensions/filters/http/jwt_authn/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

void FilterConfigImpl::init() {
  ENVOY_LOG(debug, "Loaded JwtAuthConfig: {}", proto_config_.DebugString());

  // Note: `this` and `context` have a a lifetime of the listener.
  // That may be shorter of the tls callback if the listener is torn shortly after it is created.
  // We use a shared pointer to make sure this object outlives the tls callbacks.
  auto shared_this = shared_from_this();
  tls_->set([shared_this](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalCache>(shared_this->proto_config_, shared_this->time_source_,
                                              shared_this->api_);
  });

  for (const auto& rule : proto_config_.rules()) {
    rule_pairs_.emplace_back(Matcher::create(rule),
                             Verifier::create(rule.requires(), proto_config_.providers(), *this));
  }

  if (proto_config_.has_filter_state_rules()) {
    filter_state_name_ = proto_config_.filter_state_rules().name();
    for (const auto& it : proto_config_.filter_state_rules().requires()) {
      filter_state_verifiers_.emplace(
          it.first, Verifier::create(it.second, proto_config_.providers(), *this));
    }
  }
}

const Verifier* FilterConfigImpl::findPerRouteVerifier(const PerRouteFilterConfig& per_route){
  if (per_route.config().bypass()) {
    return nullptr;
  }

  auto& cache = getCache();
  const Verifier* verifier = cache.getHashVerifier(per_route.hash());
  if (verifier == nullptr) {
    auto new_verifier = Verifier::create(per_route.config().requires(), proto_config_.providers(), *this);
    verifier = new_verifier.get();
    cache.setHashVerifier(per_route.hash(), std::move(new_verifier));
  }
  return verifier;
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
