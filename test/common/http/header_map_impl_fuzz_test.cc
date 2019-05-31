#include <functional>

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/http/header_map_impl.h"

#include "test/common/http/header_map_impl_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

using Envoy::Fuzz::replaceInvalidCharacters;

namespace Envoy {

// Fuzz the header map implementation.
DEFINE_PROTO_FUZZER(const test::common::http::HeaderMapImplFuzzTestCase& input) {
  Http::HeaderMapImplPtr header_map = std::make_unique<Http::HeaderMapImpl>();
  const auto predefined_exists = [&header_map](const std::string& s) -> bool {
    const Http::HeaderEntry* entry;
    return header_map->lookup(Http::LowerCaseString(replaceInvalidCharacters(s)), &entry) ==
           Http::HeaderMap::Lookup::Found;
  };
  std::vector<std::unique_ptr<Http::LowerCaseString>> lower_case_strings;
  std::vector<std::unique_ptr<std::string>> strings;
  constexpr auto max_actions = 1024;
  for (int i = 0; i < std::min(max_actions, input.actions().size()); ++i) {
    const auto& action = input.actions(i);
    ENVOY_LOG_MISC(debug, "Action {}", action.DebugString());
    switch (action.action_selector_case()) {
    case test::common::http::Action::kAddReference: {
      const auto& add_reference = action.add_reference();
      // Workaround for https://github.com/envoyproxy/envoy/issues/3919.
      if (predefined_exists(add_reference.key())) {
        continue;
      }
      lower_case_strings.emplace_back(
          std::make_unique<Http::LowerCaseString>(replaceInvalidCharacters(add_reference.key())));
      strings.emplace_back(
          std::make_unique<std::string>(replaceInvalidCharacters(add_reference.value())));
      header_map->addReference(*lower_case_strings.back(), *strings.back());
      break;
    }
    case test::common::http::Action::kAddReferenceKey: {
      const auto& add_reference_key = action.add_reference_key();
      // Workaround for https://github.com/envoyproxy/envoy/issues/3919.
      if (predefined_exists(add_reference_key.key())) {
        continue;
      }
      lower_case_strings.emplace_back(std::make_unique<Http::LowerCaseString>(
          replaceInvalidCharacters(add_reference_key.key())));
      switch (add_reference_key.value_selector_case()) {
      case test::common::http::AddReferenceKey::kStringValue:
        header_map->addReferenceKey(*lower_case_strings.back(),
                                    replaceInvalidCharacters(add_reference_key.string_value()));
        break;
      case test::common::http::AddReferenceKey::kUint64Value:
        header_map->addReferenceKey(*lower_case_strings.back(), add_reference_key.uint64_value());
        break;
      default:
        break;
      }
      break;
    }
    case test::common::http::Action::kAddCopy: {
      const auto& add_copy = action.add_copy();
      // Workaround for https://github.com/envoyproxy/envoy/issues/3919.
      if (predefined_exists(add_copy.key())) {
        continue;
      }
      const Http::LowerCaseString key{replaceInvalidCharacters(add_copy.key())};
      switch (add_copy.value_selector_case()) {
      case test::common::http::AddCopy::kStringValue:
        header_map->addCopy(key, replaceInvalidCharacters(add_copy.string_value()));
        break;
      case test::common::http::AddCopy::kUint64Value:
        header_map->addCopy(key, add_copy.uint64_value());
        break;
      default:
        break;
      }
      break;
    }
    case test::common::http::Action::kSetReference: {
      const auto& set_reference = action.set_reference();
      lower_case_strings.emplace_back(
          std::make_unique<Http::LowerCaseString>(replaceInvalidCharacters(set_reference.key())));
      strings.emplace_back(
          std::make_unique<std::string>(replaceInvalidCharacters(set_reference.value())));
      header_map->setReference(*lower_case_strings.back(), *strings.back());
      break;
    }
    case test::common::http::Action::kSetReferenceKey: {
      const auto& set_reference_key = action.set_reference_key();
      lower_case_strings.emplace_back(std::make_unique<Http::LowerCaseString>(
          replaceInvalidCharacters(set_reference_key.key())));
      header_map->setReferenceKey(*lower_case_strings.back(),
                                  replaceInvalidCharacters(set_reference_key.value()));
      break;
    }
    case test::common::http::Action::kGetAndMutate: {
      const auto& get_and_mutate = action.get_and_mutate();
      auto* header_entry =
          header_map->get(Http::LowerCaseString(replaceInvalidCharacters(get_and_mutate.key())));
      if (header_entry != nullptr) {
        // Do some read-only stuff.
        (void)strlen(std::string(header_entry->key().getStringView()).c_str());
        (void)strlen(std::string(header_entry->value().getStringView()).c_str());
        (void)strlen(header_entry->value().buffer());
        header_entry->key().empty();
        header_entry->value().empty();
        // Do some mutation or parameterized action.
        switch (get_and_mutate.mutate_selector_case()) {
        case test::common::http::GetAndMutate::kAppend:
          header_entry->value().append(replaceInvalidCharacters(get_and_mutate.append()).c_str(),
                                       get_and_mutate.append().size());
          break;
        case test::common::http::GetAndMutate::kClear:
          header_entry->value().clear();
          break;
        case test::common::http::GetAndMutate::kFind:
          header_entry->value().getStringView().find(get_and_mutate.find());
          break;
        case test::common::http::GetAndMutate::kSetCopy:
          header_entry->value().setCopy(replaceInvalidCharacters(get_and_mutate.set_copy()).c_str(),
                                        get_and_mutate.set_copy().size());
          break;
        case test::common::http::GetAndMutate::kSetInteger:
          header_entry->value().setInteger(get_and_mutate.set_integer());
          break;
        case test::common::http::GetAndMutate::kSetReference:
          strings.emplace_back(std::make_unique<std::string>(
              replaceInvalidCharacters(get_and_mutate.set_reference())));
          header_entry->value().setReference(*strings.back());
          break;
        default:
          break;
        }
      }
      break;
    }
    case test::common::http::Action::kCopy: {
      header_map = std::make_unique<Http::HeaderMapImpl>(
          *reinterpret_cast<Http::HeaderMap*>(header_map.get()));
      break;
    }
    case test::common::http::Action::kLookup: {
      const Http::HeaderEntry* header_entry;
      header_map->lookup(Http::LowerCaseString(replaceInvalidCharacters(action.lookup())),
                         &header_entry);
      break;
    }
    case test::common::http::Action::kRemove: {
      header_map->remove(Http::LowerCaseString(replaceInvalidCharacters(action.remove())));
      break;
    }
    case test::common::http::Action::kRemovePrefix: {
      header_map->removePrefix(
          Http::LowerCaseString(replaceInvalidCharacters(action.remove_prefix())));
      break;
    }
    default:
      // Maybe nothing is set?
      break;
    }
    // Exercise some read-only accessors.
    header_map->byteSize();
    header_map->size();
    header_map->iterate(
        [](const Http::HeaderEntry& header, void * /*context*/) -> Http::HeaderMap::Iterate {
          header.key();
          header.value();
          return Http::HeaderMap::Iterate::Continue;
        },
        nullptr);
    header_map->iterateReverse(
        [](const Http::HeaderEntry& header, void * /*context*/) -> Http::HeaderMap::Iterate {
          header.key();
          header.value();
          return Http::HeaderMap::Iterate::Continue;
        },
        nullptr);
  }
}

} // namespace Envoy
