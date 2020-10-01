#pragma once

#include <memory>
#include <variant>

#include "envoy/config/common/matcher/v3/matcher.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/matcher/matcher.h"

#include "common/common/assert.h"
#include "common/http/header_utility.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {

class HttpMatchingData : public MatchingData {
public:
  Http::RequestHeaderMap* request_headers_;
};
using HttpMatchingDataPtr = std::unique_ptr<HttpMatchingData>;

class KeyNamespaceMapper {
public:
  virtual ~KeyNamespaceMapper() = default;
  virtual void forEachValue(absl::string_view ns, absl::string_view key,
                            const MatchingData& matching_data,
                            std::function<void(absl::string_view)> value_cb) PURE;
};

class HttpKeyNamespaceMapper : public KeyNamespaceMapper {
public:
  void forEachValue(absl::string_view ns, absl::string_view key, const MatchingData& matching_data,
                    std::function<void(absl::string_view)> value_cb) override {
    const HttpMatchingData& http_data = dynamic_cast<const HttpMatchingData&>(matching_data);
    if (ns == "request_headers") {
      Http::LowerCaseString lcs((std::string(key)));
      auto* header = http_data.request_headers_->get(lcs);
      if (header) {
        value_cb(header->value().getStringView());
      }
    }
  }
};

using KeyNamespaceMapperSharedPtr = std::shared_ptr<KeyNamespaceMapper>;

class MultimapMatcher : public MatchTree {
public:
  MultimapMatcher(std::string key, std::string ns, KeyNamespaceMapperSharedPtr namespace_mapper,
                  MatchTreeSharedPtr no_match_tree)
      : key_(key), namespace_(ns), key_namespace_mapper_(std::move(namespace_mapper)),
        no_match_tree_(std::move(no_match_tree)) {}

  absl::optional<MatchAction> match(const MatchingData& data) override {
    bool first_value_evaluated = false;
    absl::optional<std::reference_wrapper<MatchTree>> selected_subtree = absl::nullopt;
    key_namespace_mapper_->forEachValue(namespace_, key_, data, [&](auto value) {
      if (first_value_evaluated) {
        return;
      }
      // TODO(snowp): Only match on the first header for now.
      first_value_evaluated = true;

      const auto itr = children_.find(value);
      if (itr != children_.end()) {
        selected_subtree = absl::make_optional(std::ref(*itr->second));
      }
    });

    if (selected_subtree) {
      return selected_subtree->get().match(data);
    }

    if (no_match_tree_) {
      return no_match_tree_->match(data);
    }

    return absl::nullopt;
  }

  void addChild(std::string value, MatchTreeSharedPtr&& subtree) {
    children_[value] = std::move(subtree);
  }

private:
  const std::string key_;
  const std::string namespace_;
  KeyNamespaceMapperSharedPtr key_namespace_mapper_;
  absl::flat_hash_map<std::string, MatchTreeSharedPtr> children_;
  MatchTreeSharedPtr no_match_tree_;
};

class AlwaysSkipMatcher : public MatchTree {
public:
  absl::optional<MatchAction> match(const MatchingData&) override { return MatchAction::skip(); }
};

class AlwaysCallbackMatcher : public MatchTree {
public:
  explicit AlwaysCallbackMatcher(std::string callback) : callback_(callback) {}

  absl::optional<MatchAction> match(const MatchingData&) override {
    return MatchAction::callback(callback_);
  }

private:
  const std::string callback_;
};

class Matcher {
public:
  virtual ~Matcher() = default;

  virtual bool match(const MatchingData& data) PURE;
};

class RequestHeaderMatcher : public Matcher {
public:
  explicit RequestHeaderMatcher(Http::HeaderUtility::HeaderDataPtr header_data)
      : header_data_(std::move(header_data)) {}

  bool match(const MatchingData& matching_data) override {
    const HttpMatchingData& http_data = dynamic_cast<const HttpMatchingData&>(matching_data);

    return header_data_->matchesHeaders(*http_data.request_headers_);
  }

  Http::HeaderUtility::HeaderDataPtr header_data_;
};

using MatcherPtr = std::unique_ptr<Matcher>;

class LeafNode : public MatchTree {
public:
  LeafNode(MatchAction match_action, absl::optional<MatchAction> no_match_action)
      : match_action_(match_action), no_match_action_(no_match_action) {}

  absl::optional<MatchAction> match(const MatchingData& matching_data) override {
    for (const auto& matcher : matchers_) {
      if (!matcher->match(matching_data)) {
        return no_match_action_;
      }
    }

    return match_action_;
  }

  void addMatcher(MatcherPtr&& matcher) { matchers_.push_back(std::move(matcher)); }

private:
  MatchAction match_action_;
  absl::optional<MatchAction> no_match_action_;
  std::vector<MatcherPtr> matchers_;
};
class MatchTreeFactory {
public:
  static MatchTreeSharedPtr create(envoy::config::common::matcher::v3::MatchTree config,
                                   KeyNamespaceMapperSharedPtr key_namespace_mapper) {
    if (config.has_matcher()) {
      return createSublinerMatcher(config.matcher(), key_namespace_mapper);
    } else if (config.has_leaf()) {
      return createLinearMatcher(config.leaf(), key_namespace_mapper);
    } else {
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

private:
  static MatchTreeSharedPtr
  createLinearMatcher(envoy::config::common::matcher::v3::MatchTree::MatchLeaf config,
                      KeyNamespaceMapperSharedPtr) {
    auto leaf = std::make_shared<LeafNode>(
        MatchAction::fromProto(config.action()),
        config.has_no_match_action()
            ? absl::make_optional(MatchAction::fromProto(config.no_match_action()))
            : absl::nullopt);

    for (const auto matcher : config.matchers()) {
      for (const auto& header : matcher.predicate().http_request_headers_match().headers()) {
        leaf->addMatcher(std::make_unique<RequestHeaderMatcher>(
            std::make_unique<Http::HeaderUtility::HeaderData>(header)));
      }
    }

    return leaf;
  }

  static MatchTreeSharedPtr
  createSublinerMatcher(envoy::config::common::matcher::v3::MatchTree::SublinearMatcher matcher,
                        KeyNamespaceMapperSharedPtr key_namespace_mapper) {
    // TODO(snowp): Suppor extensions, we only support multi map right now.

    auto multimap_matcher = std::make_shared<MultimapMatcher>(
        matcher.multimap_matcher().key(), matcher.multimap_matcher().namespace_(),
        key_namespace_mapper,
        matcher.has_no_match_tree()
            ? MatchTreeFactory::create(matcher.no_match_tree(), key_namespace_mapper)
            : nullptr);

    for (const auto& children : matcher.multimap_matcher().exact_matches()) {
      multimap_matcher->addChild(children.first,
                                 MatchTreeFactory::create(children.second, key_namespace_mapper));
    }

    return multimap_matcher;
  }
};
} // namespace Envoy