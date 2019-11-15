#include "server/tag_generator_batch_impl.h"

namespace Envoy {
namespace Server {
TagGeneratorBatchImpl::~TagGeneratorBatchImpl() = default;

TagGenerator::Tags TagGeneratorBatchImpl::addFilterChains(
    absl::Span<const ::envoy::api::v2::listener::FilterChain* const> filter_chain_span) {
  TagGenerator::Tags tags;
  for (const auto& fc : filter_chain_span) {
    const auto& kv = filter_chains_.emplace(*fc, ++next_tag_);
    tags.insert(kv.second ? next_tag_ : kv.first->second);
  }
  return tags;
}

TagGenerator::Tags TagGeneratorBatchImpl::getTags() {
  TagGenerator::Tags res;
  for (const auto& kv : filter_chains_) {
    res.insert(kv.second);
  }
  return res;
}

} // namespace Server
} // namespace Envoy
