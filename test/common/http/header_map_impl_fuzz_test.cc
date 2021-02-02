#include <functional>

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/http/header_map_impl.h"

#include "test/common/http/header_map_impl_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/test_common/test_runtime.h"

#include "absl/strings/ascii.h"

using Envoy::Fuzz::replaceInvalidCharacters;

namespace Envoy {

// Fuzz the header map implementation.
DEFINE_PROTO_FUZZER(const test::common::http::HeaderMapImplFuzzTestCase& input) {
  TestScopedRuntime runtime;
  // Set the lazy header-map threshold if found.
  if (input.has_config()) {
    Runtime::LoaderSingleton::getExisting()->mergeValues(
        {{"envoy.http.headermap.lazy_map_min_size",
          absl::StrCat(input.config().lazy_map_min_size())}});
  }

  auto header_map = Http::RequestHeaderMapImpl::create();
  std::vector<std::unique_ptr<Http::LowerCaseString>> lower_case_strings;
  std::vector<std::unique_ptr<std::string>> strings;
  uint64_t set_integer;
  constexpr auto max_actions = 128;
  for (int i = 0; i < std::min(max_actions, input.actions().size()); ++i) {
    const auto& action = input.actions(i);
    ENVOY_LOG_MISC(debug, "Action {}", action.DebugString());
    switch (action.action_selector_case()) {
    case test::common::http::Action::kAddReference: {
      const auto& add_reference = action.add_reference();
      lower_case_strings.emplace_back(
          std::make_unique<Http::LowerCaseString>(replaceInvalidCharacters(add_reference.key())));
      strings.emplace_back(
          std::make_unique<std::string>(replaceInvalidCharacters(add_reference.value())));
      header_map->addReference(*lower_case_strings.back(), *strings.back());
      break;
    }
    case test::common::http::Action::kAddReferenceKey: {
      const auto& add_reference_key = action.add_reference_key();
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
    case test::common::http::Action::kGet: {
      const auto& get = action.get();
      const auto header_entry =
          header_map->get(Http::LowerCaseString(replaceInvalidCharacters(get.key())));
      for (size_t i = 0; i < header_entry.size(); i++) {
        // Do some read-only stuff.
        (void)strlen(std::string(header_entry[i]->key().getStringView()).c_str());
        (void)strlen(std::string(header_entry[i]->value().getStringView()).c_str());
        header_entry[i]->key().empty();
        header_entry[i]->value().empty();
      }
      break;
    }
    case test::common::http::Action::kMutateAndMove: {
      const auto& mutate_and_move = action.mutate_and_move();
      lower_case_strings.emplace_back(
          std::make_unique<Http::LowerCaseString>(replaceInvalidCharacters(mutate_and_move.key())));
      // Randomly (using fuzzer data) set the header_field to either be of type Reference or Inline
      const auto& str = lower_case_strings.back();
      Http::HeaderString header_field; // By default it's Inline
      if ((!str->get().empty()) && (str->get().at(0) & 0x1)) {
        // Keeping header_field as Inline
        header_field.setCopy(str->get());
        // inlineTransform can only be applied to Inline type!
        header_field.inlineTransform(absl::ascii_tolower);
      } else {
        // Changing header_field to Reference
        header_field.setReference(str->get());
      }
      Http::HeaderString header_value;
      // Do some mutation or parameterized action.
      switch (mutate_and_move.mutate_selector_case()) {
      case test::common::http::MutateAndMove::kAppend:
        header_value.append(replaceInvalidCharacters(mutate_and_move.append()).c_str(),
                            mutate_and_move.append().size());
        break;
      case test::common::http::MutateAndMove::kSetCopy:
        header_value.setCopy(replaceInvalidCharacters(mutate_and_move.set_copy()));
        break;
      case test::common::http::MutateAndMove::kSetInteger:
        set_integer = mutate_and_move.set_integer();
        header_value.setInteger(set_integer);
        break;
      case test::common::http::MutateAndMove::kSetReference:
        strings.emplace_back(std::make_unique<std::string>(
            replaceInvalidCharacters(mutate_and_move.set_reference())));
        header_value.setReference(*strings.back());
        break;
      default:
        break;
      }
      // Can't addViaMove on an empty header value.
      if (!header_value.empty()) {
        header_map->addViaMove(std::move(header_field), std::move(header_value));
      }
      break;
    }
    case test::common::http::Action::kAppend: {
      const auto& append = action.append();
      lower_case_strings.emplace_back(
          std::make_unique<Http::LowerCaseString>(replaceInvalidCharacters(append.key())));
      strings.emplace_back(std::make_unique<std::string>(replaceInvalidCharacters(append.value())));
      header_map->appendCopy(*lower_case_strings.back(), *strings.back());
      break;
    }
    case test::common::http::Action::kCopy: {
      header_map = Http::createHeaderMap<Http::RequestHeaderMapImpl>(*header_map);
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
    case test::common::http::Action::kGetAndMutate: {
      // Deprecated. Can not get and mutate entries.
      break;
    }
    default:
      // Maybe nothing is set?
      break;
    }
    // Exercise some read-only accessors.
    header_map->size();
    header_map->byteSize();
    header_map->iterate([](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
      header.key();
      header.value();
      return Http::HeaderMap::Iterate::Continue;
    });
    header_map->iterateReverse([](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
      header.key();
      header.value();
      return Http::HeaderMap::Iterate::Continue;
    });
  }
}

} // namespace Envoy
