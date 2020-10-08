#include "common/matcher/matcher.h"

namespace Envoy {
MatchTreeSharedPtr
MatchTreeFactory::create(const envoy::config::common::matcher::v3::MatchTree& config,
                         KeyNamespaceMapperSharedPtr key_namespace_mapper,
                         MatchTreeFactoryCallbacks& callbacks) {
  if (config.has_matcher()) {
    return createSublinerMatcher(config.matcher(), key_namespace_mapper, callbacks);
  } else if (config.has_leaf()) {
    return createLinearMatcher(config.leaf(), key_namespace_mapper, callbacks);
  } else {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

MatchTreeSharedPtr MatchTreeFactory::createLinearMatcher(
    const envoy::config::common::matcher::v3::MatchTree::MatchLeaf& config,
    KeyNamespaceMapperSharedPtr, MatchTreeFactoryCallbacks& callbacks) {
  auto leaf = std::make_shared<LeafNode>(
      config.has_no_match_action()
          ? absl::make_optional(MatchAction::fromProto(config.no_match_action()))
          : absl::nullopt);

  for (const auto& matcher : config.matchers()) {
    auto predicate_matcher = std::make_shared<MatchWrapper>(matcher.predicate());
    callbacks.addPredicateMatcher(predicate_matcher);
    leaf->addMatcher(std::make_unique<HttpPredicateMatcher>(predicate_matcher),
                     MatchAction::fromProto(matcher.action()));
  }

  return leaf;
}

MatchTreeSharedPtr MatchTreeFactory::createSublinerMatcher(
    const envoy::config::common::matcher::v3::MatchTree::SublinearMatcher& matcher,
    KeyNamespaceMapperSharedPtr key_namespace_mapper, MatchTreeFactoryCallbacks& callbacks) {
  auto multimap_matcher = std::make_shared<MultimapMatcher>(
      matcher.multimap_matcher().key(), matcher.multimap_matcher().key_namespace(),
      key_namespace_mapper,
      matcher.has_no_match_tree()
          ? MatchTreeFactory::create(matcher.no_match_tree(), key_namespace_mapper, callbacks)
          : nullptr);

  for (const auto& children : matcher.multimap_matcher().exact_matches()) {
    multimap_matcher->addChild(
        children.first, MatchTreeFactory::create(children.second, key_namespace_mapper, callbacks));
  }

  return multimap_matcher;
}
} // namespace Envoy