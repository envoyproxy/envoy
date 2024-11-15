#include "validated_input_generator.h"

#include "source/common/protobuf/visitor_helper.h"

#include "xds/type/matcher/v3/cel.pb.h"
#include "xds/type/matcher/v3/domain.pb.h"
#include "xds/type/matcher/v3/http_inputs.pb.h"
#include "xds/type/matcher/v3/ip.pb.h"
#include "xds/type/matcher/v3/matcher.pb.h"
#include "xds/type/matcher/v3/range.pb.h"
#include "xds/type/matcher/v3/regex.pb.h"
#include "xds/type/matcher/v3/string.pb.h"

namespace Envoy {
namespace ProtobufMessage {

const std::string ValidatedInputGenerator::kAny = "google.protobuf.Any";

ValidatedInputGenerator::ValidatedInputGenerator(unsigned int seed, AnyMap&& default_any_map,
                                                 unsigned int max_depth)
    : current_depth_(0), max_depth_(max_depth), any_map_(std::move(default_any_map)) {
  random_.initializeSeed(seed);
  mutator_.Seed(seed);
}

template <typename T, typename R> static bool handleNumericRules(T& number, const R& number_rules) {
  if (number_rules.has_ignore_empty() && number_rules.ignore_empty()) {
    return false;
  }
  if (number_rules.has_const_() && number != number_rules.const_()) {
    number = number_rules.const_();
  }
  if (number_rules.has_lt() && number >= number_rules.lt()) {
    number = number_rules.lt() - 1;
  }
  if (number_rules.has_lte() && number > number_rules.lte()) {
    number = number_rules.lte();
  }
  if (number_rules.has_gt() && number <= number_rules.gt()) {
    number = number_rules.gt() + 1;
  }
  if (number_rules.has_gte() && number < number_rules.gte()) {
    number = number_rules.gte();
  }
  if (number_rules.in_size() > 0 || number_rules.not_in_size() > 0) {
    ENVOY_LOG_MISC(info, "pgv::U32IntRules::in|not_in rule found, not handling yet");
  }
  return true;
}

static void handleStringRules(std::string& str, const validate::StringRules& string_rules) {
  // Multiple rules could be present, therefore use an if and not a switch.
  // Go by ascending order of proto-field-index.
  if (string_rules.has_const_()) {
    str = string_rules.const_();
  }
  const size_t c_len =
      string_rules.has_min_len() || string_rules.has_max_len() ? pgv::Utf8Len(str) : 0;
  if (string_rules.has_min_len() && c_len < string_rules.min_len()) {
    // just fill up with 'a's for simplicity
    str += std::string(string_rules.min_len() - c_len, 'a');
  }
  if (string_rules.has_max_len() && c_len > string_rules.max_len()) {
    const char* codepoint_ptr = str.c_str();
    ptrdiff_t byte_len = str.length();
    size_t unicode_len = 0;
    int char_len = 0;
    while (byte_len > 0 && unicode_len < string_rules.max_len()) {
      char_len = pgv::UTF8FirstLetterNumBytes(codepoint_ptr, byte_len);
      codepoint_ptr += char_len;
      byte_len -= char_len;
      ++unicode_len;
    }
    str = std::string(str.c_str(), codepoint_ptr);
  }
  if (string_rules.has_min_bytes() && str.length() < string_rules.min_bytes()) {
    // just fill up with 'a's for simplicity
    str += std::string(string_rules.min_bytes() - str.length(), 'a');
  }
  if (string_rules.has_max_bytes() && str.length() > string_rules.max_bytes()) {
    str = str.substr(0, string_rules.max_bytes());
  }
  if (string_rules.has_pattern()) {
    ENVOY_LOG_MISC(info, "pgv::StringRules::pattern '{}' found, not handling yet",
                   string_rules.pattern());
  }
  if (string_rules.has_prefix() &&
      str.substr(0, string_rules.prefix().length()) != string_rules.prefix()) {
    if (str.length() < string_rules.prefix().length()) {
      str = string_rules.prefix();
    } else {
      str.replace(0, string_rules.prefix().length(), string_rules.prefix());
    }
  }
  if (string_rules.has_suffix() && str.length() >= string_rules.suffix().length() &&
      str.substr(str.length() - string_rules.suffix().length(), string_rules.suffix().length()) !=
          string_rules.suffix()) {
    if (str.length() < string_rules.suffix().length()) {
      str = string_rules.suffix();
    } else {
      str.replace(str.length() - string_rules.suffix().length(), string_rules.suffix().length(),
                  string_rules.suffix());
    }
  }
  if (string_rules.has_contains() &&
      str.find_first_of(string_rules.contains(), 0) == std::string::npos) {
    str += string_rules.contains();
  }
  if (string_rules.in_size() != 0 || string_rules.not_in_size() != 0) {
    ENVOY_LOG_MISC(info, "pgv::StringRules::in|not_in rule found, not handling yet");
  }
  if (string_rules.has_email()) {
    ENVOY_LOG_MISC(info, "pgv::StringRules::email rule found, not handling yet");
  }
  if (string_rules.has_hostname() && !pgv::IsHostname(str)) {
    str = "localhost.localdomain";
  }
  if ((string_rules.has_ip() && !pgv::IsIp(str)) ||
      (string_rules.has_ipv4() && !pgv::IsIpv4(str))) {
    str = "127.0.0.1";
  }
  if (string_rules.has_ipv6() && !pgv::IsIpv6(str)) {
    str = "::1";
  }
  if (string_rules.has_uri()) {
    ENVOY_LOG_MISC(info, "pgv::StringRules::uri rule found, not handling yet");
  }
  if (string_rules.has_uri_ref()) {
    ENVOY_LOG_MISC(info, "pgv::StringRules::uri_ref rule found, not handling yet");
  }
  if (string_rules.has_len()) {
    ENVOY_LOG_MISC(info, "pgv::StringRules::len rule found, not handling yet");
  }
  if (string_rules.has_len_bytes() && str.length() != string_rules.len_bytes()) {

    if (str.length() < string_rules.len_bytes()) {
      str += std::string(string_rules.len_bytes() - str.length(), 'a');
    } else {
      str = str.substr(0, string_rules.len_bytes());
    }
  }

  if (string_rules.has_address()) {
    ENVOY_LOG_MISC(info, "pgv::StringRules::address rule found, not handling yet");
  }
  if (string_rules.has_uuid()) {
    ENVOY_LOG_MISC(info, "pgv::StringRules::uuid rule found, not handling yet");
  }
  {
    std::string::size_type found_at;
    if (string_rules.has_not_contains() &&
        ((found_at = str.find_first_of(string_rules.not_contains(), 0))) != std::string::npos) {
      str[found_at] += 1; // just increase the first character's byte. Yes, quite ugly.
    }
  }
  if (string_rules.has_well_known_regex()) {
    ENVOY_LOG_MISC(info, "pgv::StringRules::well_known_regex rule found, not handling yet");
  }
}

void ValidatedInputGenerator::handleAnyRules(
    Protobuf::Message* msg, const validate::AnyRules& any_rules,
    const absl::Span<const Protobuf::Message* const>& parents) {
  if (any_rules.has_required() && any_rules.required()) {
    // Stop creating any message when a certain depth is reached
    if (max_depth_ > 0 && current_depth_ > max_depth_) {
      auto* any_message = Protobuf::DynamicCastMessage<ProtobufWkt::Any>(msg);
      any_message->PackFrom(ProtobufWkt::Struct());
      return;
    }
    const Protobuf::Descriptor* descriptor = msg->GetDescriptor();
    std::unique_ptr<Protobuf::Message> inner_message;
    if (descriptor->full_name() == kAny) {
      const std::string class_name = parents.back()->GetDescriptor()->full_name();
      AnyMap::const_iterator any_map_cand = any_map_.find(class_name);
      if (any_map_cand != any_map_.end()) {
        const FieldToTypeUrls& field_to_typeurls = any_map_cand->second;
        const std::string field_name = std::string(message_path_.back());
        FieldToTypeUrls::const_iterator field_to_typeurls_cand = field_to_typeurls.find(field_name);
        if (field_to_typeurls_cand != any_map_cand->second.end()) {
          auto* any_message = Protobuf::DynamicCastMessage<ProtobufWkt::Any>(msg);
          inner_message = ProtobufMessage::Helper::typeUrlToMessage(any_message->type_url());
          if (!inner_message || !any_message->UnpackTo(inner_message.get())) {
            const TypeUrlAndFactory& randomed_typeurl = field_to_typeurls_cand->second.at(
                random_() % field_to_typeurls_cand->second.size());
            any_message->set_type_url(absl::StrCat("type.googleapis.com/", randomed_typeurl.first));
            auto prototype = randomed_typeurl.second();
            ASSERT(prototype);
            any_message->PackFrom(*prototype);
          }
          return;
        }
      }
      ENVOY_LOG_MISC(info, "!!!! NOT FOUND parent of Any is: {}..{}",
                     parents[parents.size() - 2]->GetDescriptor()->full_name(),
                     message_path_[message_path_.size() - 2]);
      const Protobuf::Descriptor* par_desc = parents.back()->GetDescriptor();
      ENVOY_LOG_MISC(info, "!!!! NOT FOUND in class {}", par_desc->full_name());
    }
  }
  if (any_rules.in_size() > 0 || any_rules.not_in_size() > 0) {
    ENVOY_LOG_MISC(info, "pgv::AnyRules::in|not_in rule found, not handling yet");
  }
}

void ValidatedInputGenerator::handleMessageTypedField(
    Protobuf::Message& msg, const Protobuf::FieldDescriptor& field,
    const Protobuf::Reflection* reflection, const validate::FieldRules& rules,
    const absl::Span<const Protobuf::Message* const>& parents, bool force_create, bool cut_off) {

  if (field.is_repeated()) {
    const validate::RepeatedRules& repeated_rules = rules.repeated();
    std::uint64_t repeat_total64 = static_cast<std::uint64_t>(reflection->FieldSize(msg, &field));
    const bool ignore_empty = repeated_rules.has_ignore_empty() && repeated_rules.ignore_empty();
    if (ignore_empty && repeat_total64 == 0) {
      return;
    }
    if (repeated_rules.has_min_items() && repeated_rules.min_items() > repeat_total64) {
      for (; repeated_rules.min_items() > repeat_total64; ++repeat_total64) {
        reflection->AddMessage(&msg, &field);
      }
    }
    if (repeated_rules.has_max_items() && repeated_rules.max_items() <= repeat_total64) {
      for (; repeat_total64 > repeated_rules.max_items(); --repeat_total64) {
        reflection->RemoveLast(&msg, &field);
      }
    }
    if (repeated_rules.has_unique()) {
      ENVOY_LOG_MISC(debug,
                     "repeated protobuf validation rule 'unique' found, but not supported yet");
    }
    // The visitor will traverse over all repeated entries anyway. Therefore no
    // need to traverse here.
  } else {
    if (force_create || reflection->HasField(msg, &field) ||
        (rules.message().has_required() && rules.message().required()) ||
        (rules.message().IsInitialized())) {
      // Enforce that the msg for the given field follows the validation rules, if
      // - the member is set already, or
      // - needs to be set, but is not, or
      // - there are rules to apply.
      Protobuf::Message* value = reflection->MutableMessage(&msg, &field);
      if (value->GetDescriptor()->options().HasExtension(validate::disabled) &&
          value->GetDescriptor()->options().GetExtension(validate::disabled)) {
        return;
      }
      switch (rules.type_case()) {
      case validate::FieldRules::kAny: {
        handleAnyRules(value, rules.any(), parents);
        break;
      }
      default:
        if (cut_off) {
          value->Clear();
        }
        break;
      }
    }
  }
}

// Handle all validation rules for intrinsic types like int, uint and string.
// Messages are more complicated to handle and can not be handled here.
template <typename T, auto FIELDGETTER, auto FIELDSETTER, auto REPGETTER, auto REPSETTER,
          auto FIELDADDER, auto RULEGETTER, auto TYPEHANDLER>
void ValidatedInputGenerator::handleIntrinsicTypedField(Protobuf::Message& msg,
                                                        const Protobuf::FieldDescriptor& field,
                                                        const Protobuf::Reflection* reflection,
                                                        const validate::FieldRules& rules,
                                                        bool force) {

  if (field.is_repeated()) {
    const validate::RepeatedRules& repeated_rules = rules.repeated();
    std::uint64_t repeat_total64 = static_cast<std::uint64_t>(reflection->FieldSize(msg, &field));
    const bool ignore_empty = repeated_rules.has_ignore_empty() && repeated_rules.ignore_empty();
    if (ignore_empty && repeat_total64 == 0) {
      return;
    }
    if (repeated_rules.has_min_items() && repeated_rules.min_items() > repeat_total64) {
      for (; repeated_rules.min_items() > repeat_total64; ++repeat_total64) {
        const T value{};
        (*reflection.*FIELDADDER)(&msg, &field, value);
      }
    }
    if (repeated_rules.has_max_items() && repeated_rules.max_items() <= repeat_total64) {
      for (; repeat_total64 > repeated_rules.max_items(); --repeat_total64) {
        reflection->RemoveLast(&msg, &field);
      }
    }
    if (repeated_rules.has_unique()) {
      ENVOY_LOG_MISC(debug,
                     "repeated protobuf validation rule 'unique' found, but not supported yet");
    }
    if (repeated_rules.has_items()) {
      const int repeat_total = reflection->FieldSize(msg, &field);
      for (int repeat_cnt = 0; repeat_cnt < repeat_total; ++repeat_cnt) {
        T value = (*reflection.*REPGETTER)(msg, &field, repeat_cnt);
        TYPEHANDLER(value, (repeated_rules.items().*RULEGETTER)());
        (*reflection.*REPSETTER)(&msg, &field, repeat_cnt, value);
      }
    }
  } else {
    if (force || reflection->HasField(msg, &field) ||
        (rules.message().has_required() && rules.message().required()) ||
        ((rules.*RULEGETTER)().IsInitialized())) {
      // Enforce that the msg for the given field follows the validation rules, if
      // - the member is set already, or
      // - needs to be set, but is not, or
      // - there are rules to apply.
      T value = (*reflection.*FIELDGETTER)(msg, &field);
      TYPEHANDLER(value, (rules.*RULEGETTER)());
      (*reflection.*FIELDSETTER)(&msg, &field, value);
    }
  }
}

void ValidatedInputGenerator::onField(Protobuf::Message& msg,
                                      const Protobuf::FieldDescriptor& field,
                                      const absl::Span<const Protobuf::Message* const> parents) {
  onField(msg, field, parents, false, false);
}

void ValidatedInputGenerator::onField(Protobuf::Message& msg,
                                      const Protobuf::FieldDescriptor& field,
                                      const absl::Span<const Protobuf::Message* const> parents,
                                      bool force_create, bool cut_off) {
  const Protobuf::Reflection* reflection = msg.GetReflection();

  if (!field.options().HasExtension(validate::rules) && !force_create) {
    return;
  }
  const validate::FieldRules& rules = field.options().GetExtension(validate::rules);
  if (rules.message().has_skip() && rules.message().skip()) {
    return;
  }

  switch (field.cpp_type()) {
  case Protobuf::FieldDescriptor::CPPTYPE_INT32: {
    handleIntrinsicTypedField<
        std::int32_t, &Protobuf::Reflection::GetInt32, &Protobuf::Reflection::SetInt32,
        &Protobuf::Reflection::GetRepeatedInt32, &Protobuf::Reflection::SetRepeatedInt32,
        &Protobuf::Reflection::AddInt32, &validate::FieldRules::int32>(msg, field, reflection,
                                                                       rules, force_create);
    break;
  }
  case Protobuf::FieldDescriptor::CPPTYPE_INT64: {
    handleIntrinsicTypedField<
        std::int64_t, &Protobuf::Reflection::GetInt64, &Protobuf::Reflection::SetInt64,
        &Protobuf::Reflection::GetRepeatedInt64, &Protobuf::Reflection::SetRepeatedInt64,
        &Protobuf::Reflection::AddInt64, &validate::FieldRules::int64>(msg, field, reflection,
                                                                       rules, force_create);
    break;
  }
  case Protobuf::FieldDescriptor::CPPTYPE_UINT32: {
    handleIntrinsicTypedField<
        std::uint32_t, &Protobuf::Reflection::GetUInt32, &Protobuf::Reflection::SetUInt32,
        &Protobuf::Reflection::GetRepeatedUInt32, &Protobuf::Reflection::SetRepeatedUInt32,
        &Protobuf::Reflection::AddUInt32, &validate::FieldRules::uint32>(msg, field, reflection,
                                                                         rules, force_create);
    break;
  }
  case Protobuf::FieldDescriptor::CPPTYPE_UINT64: {
    handleIntrinsicTypedField<
        std::uint64_t, &Protobuf::Reflection::GetUInt64, &Protobuf::Reflection::SetUInt64,
        &Protobuf::Reflection::GetRepeatedUInt64, &Protobuf::Reflection::SetRepeatedUInt64,
        &Protobuf::Reflection::AddUInt64, &validate::FieldRules::uint64>(msg, field, reflection,
                                                                         rules, force_create);
    break;
  }
  case Protobuf::FieldDescriptor::CPPTYPE_DOUBLE: {
    handleIntrinsicTypedField<
        double, &Protobuf::Reflection::GetDouble, &Protobuf::Reflection::SetDouble,
        &Protobuf::Reflection::GetRepeatedDouble, &Protobuf::Reflection::SetRepeatedDouble,
        &Protobuf::Reflection::AddDouble, &validate::FieldRules::double_>(msg, field, reflection,
                                                                          rules, force_create);
    break;
  }
  case Protobuf::FieldDescriptor::CPPTYPE_FLOAT: {
    handleIntrinsicTypedField<
        float, &Protobuf::Reflection::GetFloat, &Protobuf::Reflection::SetFloat,
        &Protobuf::Reflection::GetRepeatedFloat, &Protobuf::Reflection::SetRepeatedFloat,
        &Protobuf::Reflection::AddFloat, &validate::FieldRules::float_>(msg, field, reflection,
                                                                        rules, force_create);
    break;
  }
  case Protobuf::FieldDescriptor::CPPTYPE_BOOL:
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_ENUM:
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_STRING: {
    handleIntrinsicTypedField<
        std::string, &Protobuf::Reflection::GetString,
        static_cast<void (Protobuf::Reflection::*)(
            Protobuf::Message*, const Protobuf::FieldDescriptor*, std::string) const>(
            &Protobuf::Reflection::SetString),
        &Protobuf::Reflection::GetRepeatedString, &Protobuf::Reflection::SetRepeatedString,
        &Protobuf::Reflection::AddString, &validate::FieldRules::string, &handleStringRules>(
        msg, field, reflection, rules, force_create);
    break;
  }
  case Protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
    handleMessageTypedField(msg, field, reflection, rules, parents, force_create, cut_off);
    break;
  }
  default:
    break;
  }
}

void ValidatedInputGenerator::onEnterMessage(Protobuf::Message& msg,
                                             absl::Span<const Protobuf::Message* const> parents,
                                             bool, absl::string_view field_name) {
  ++current_depth_;
  const Protobuf::Reflection* reflection = msg.GetReflection();
  const Protobuf::Descriptor* descriptor = msg.GetDescriptor();
  message_path_.push_back(field_name);
  if (descriptor->full_name() == kAny) {
    auto* any_message = Protobuf::DynamicCastMessage<ProtobufWkt::Any>(&msg);
    std::unique_ptr<Protobuf::Message> inner_message =
        ProtobufMessage::Helper::typeUrlToMessage(any_message->type_url());
    if (!inner_message || !any_message->UnpackTo(inner_message.get())) {
      any_message->Clear();
    }
  }
  const bool max_depth_exceeded = max_depth_ > 0 && current_depth_ > max_depth_;
  for (int oneof_index = 0; oneof_index < descriptor->oneof_decl_count(); ++oneof_index) {
    const Protobuf::OneofDescriptor* oneof_desc = descriptor->oneof_decl(oneof_index);
    if (max_depth_exceeded || (oneof_desc->options().HasExtension(validate::required) &&
                               oneof_desc->options().GetExtension(validate::required) &&
                               !reflection->HasOneof(msg, descriptor->oneof_decl(oneof_index)))) {
      // No required member in one of set, so create one.
      for (int index = 0; index < oneof_desc->field_count(); ++index) {
        const std::string class_name = descriptor->full_name();
        // Treat matchers special, because in their oneof they reference themselves, which may
        // create long chains. Prefer the first alternative, which does not reference itself.
        // Nevertheless do it randomly to allow for some nesting.
        if ((class_name == "xds.type.matcher.v3.Matcher.MatcherList.Predicate" ||
             class_name == "xds.type.matcher.v3.Matcher.MatcherList.Predicate.SinglePredicate") &&
            (random_() % 200) > 0) {
          onField(msg, *oneof_desc->field(0), parents, true, max_depth_exceeded);
        } else {
          // Do not use the first available alternative all the time, because of cyclic
          // dependencies.
          const int rnd_index = random_() % oneof_desc->field_count();
          onField(msg, *oneof_desc->field(rnd_index), parents, true, max_depth_exceeded);
        }
        // Check if for the above field an entry could be created and quit the inner loop if so.
        // It might not be possible, when the datatype is not supported (yet).
        if (reflection->HasOneof(msg, descriptor->oneof_decl(oneof_index))) {
          break;
        }
      }
    }
  }
}

void ValidatedInputGenerator::onLeaveMessage(Protobuf::Message&,
                                             absl::Span<const Protobuf::Message* const>, bool,
                                             absl::string_view) {
  message_path_.pop_back();
  --current_depth_;
}

ValidatedInputGenerator::AnyMap ValidatedInputGenerator::getDefaultAnyMap() {

  static const auto dummy_proto_msg = []() -> std::unique_ptr<Protobuf::Message> {
    return std::make_unique<ProtobufWkt::Struct>();
  };

  static const ValidatedInputGenerator::ListOfTypeUrlAndFactory matchers = {
      {"xds.type.matcher.v3.CelMatcher",
       []() -> std::unique_ptr<Protobuf::Message> {
         return std::make_unique<xds::type::matcher::v3::CelMatcher>();
       }},
      {"xds.type.matcher.v3.ServerNameMatcher",
       []() -> std::unique_ptr<Protobuf::Message> {
         return std::make_unique<xds::type::matcher::v3::ServerNameMatcher>();
       }},
      {"xds.type.matcher.v3.HttpAttributesCelMatchInput",
       []() -> std::unique_ptr<Protobuf::Message> {
         return std::make_unique<xds::type::matcher::v3::HttpAttributesCelMatchInput>();
       }},
      {"xds.type.matcher.v3.IPMatcher",
       []() -> std::unique_ptr<Protobuf::Message> {
         return std::make_unique<xds::type::matcher::v3::IPMatcher>();
       }},
      {"xds.type.matcher.v3.RegexMatcher",
       []() -> std::unique_ptr<Protobuf::Message> {
         return std::make_unique<xds::type::matcher::v3::RegexMatcher>();
       }},
      {"xds.type.matcher.v3.StringMatcher", []() -> std::unique_ptr<Protobuf::Message> {
         return std::make_unique<xds::type::matcher::v3::StringMatcher>();
       }}};

  static const ValidatedInputGenerator::ListOfTypeUrlAndFactory input_matchers = {
      {"xds.type.matcher.v3.StringMatcher", []() -> std::unique_ptr<Protobuf::Message> {
         return std::make_unique<xds::type::matcher::v3::StringMatcher>();
       }}};

  static const ValidatedInputGenerator::ListOfTypeUrlAndFactory actions = {
      {"envoy.config.core.v3.SubstitutionFormatString", dummy_proto_msg}};

  static const ValidatedInputGenerator::AnyMap any_map = {
      {"xds.type.matcher.v3.Matcher", {{"on_no_match", matchers}}},
      {"xds.type.matcher.v3.Matcher.OnMatch", {{"action", actions}}},
      {"xds.type.matcher.v3.Matcher.MatcherTree",
       {{"input", input_matchers}, {"custom_match", input_matchers}}},
      {"xds.type.matcher.v3.Matcher.MatcherList.Predicate.SinglePredicate",
       {{"custom_match", input_matchers}, {"input", input_matchers}}}};
  return any_map;
}

} // namespace ProtobufMessage
} // namespace Envoy
