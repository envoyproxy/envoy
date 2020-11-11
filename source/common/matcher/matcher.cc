#include "common/matcher/matcher.h"

#include "common/common/assert.h"
#include "common/config/utility.h"

namespace Envoy {
MatchTreeSharedPtr
MatchTreeFactory::create(const envoy::config::common::matcher::v3::Matcher& config) {
  if (config.has_matcher_tree()) {
    return createTreeMatcher(config);
  } else if (config.has_matcher_list()) {
    return createListMatcher(config);
  } else {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

MatchTreeSharedPtr
MatchTreeFactory::createListMatcher(const envoy::config::common::matcher::v3::Matcher& config) {
  auto leaf = std::make_shared<ListMatcher>(createOnMatch(config.on_no_match()));

  // for (const auto& _ : config.matcher_list().matchers()) {
  //   // TODO
  // }

  return leaf;
}

MatchTreeSharedPtr
MatchTreeFactory::createTreeMatcher(const envoy::config::common::matcher::v3::Matcher& matcher) {
  auto multimap_matcher = std::make_shared<MultimapMatcher>(
      createDataInput(matcher.matcher_tree().input()), createOnMatch(matcher.on_no_match()));

  for (const auto& children : matcher.matcher_tree().exact_match_map().map()) {
    multimap_matcher->addChild(children.first, MatchTreeFactory::createOnMatch(children.second));
  }

  return multimap_matcher;
}
OnMatch MatchTreeFactory::createOnMatch(
    const envoy::config::common::matcher::v3::Matcher::OnMatch& on_match) {
  if (on_match.has_matcher()) {
    return {{}, create(on_match.matcher())};
  } else if (on_match.has_action()) {
    return {on_match.action(), {}};
  } else {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

DataInputPtr
MatchTreeFactory::createDataInput(const envoy::config::core::v3::TypedExtensionConfig& config) {
  auto& factory = Config::Utility::getAndCheckFactory<DataInputFactory>(config);
  return factory.create();
}
} // namespace Envoy