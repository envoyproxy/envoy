#pragma once

#include "envoy/api/v2/listener/listener.pb.h"

#include "common/protobuf/utility.h"

#include "tag_generator.h"

namespace Envoy {
namespace Server {

/**
 * Generate tag for filter chain. The tag is used to identify the connections belong to the tag
 * asociated filter chain. If two filter chains are different, their tags must be different. Two
 * identical filter chains should shared the same tag but it is not required.
 */
class TagGeneratorBatchImpl : public TagGenerator {
public:
  ~TagGeneratorBatchImpl() override;
  TagGenerator::Tags getTags() override;
  TagGenerator::Tags addFilterChains(
      absl::Span<const ::envoy::api::v2::listener::FilterChain* const> filter_chain_span);

private:
  std::unordered_map<envoy::api::v2::listener::FilterChain, uint64_t, MessageUtil, MessageUtil>
      filter_chains_;
  uint64_t next_tag_{0};
};
} // namespace Server
} // namespace Envoy
