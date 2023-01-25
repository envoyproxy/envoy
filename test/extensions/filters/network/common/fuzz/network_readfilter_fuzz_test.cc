#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/config/utility.h"
#include "test/extensions/filters/network/common/fuzz/network_readfilter_fuzz.pb.validate.h"
#include "test/extensions/filters/network/common/fuzz/uber_readfilter.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/test_runtime.h"

// for GenerateValidMessage-Visitor
#include "source/common/protobuf/visitor.h"
#include "src/libfuzzer/libfuzzer_mutator.h"
#include "validate/validate.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {

class GenerateValidMessage : public ProtobufMessage::ProtoVisitor, private pgv::BaseValidator {
#define MYLOGLEV error
public:
  class Mutator : public protobuf_mutator::libfuzzer::Mutator {
  public:
    using protobuf_mutator::libfuzzer::Mutator::Mutator;

    using protobuf_mutator::libfuzzer::Mutator::MutateString;
  };
  GenerateValidMessage(unsigned int seed) { mutator_.Seed(seed); }

  template <typename T, typename R>
  static bool handle_numeric_rules(T& number, const R& number_rules) {
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
      ENVOY_LOG_MISC(MYLOGLEV, "pgv::U32IntRules::in|not_in rule found, not handling yet");
    }
    return true;
  }

  static void handle_string_rules(std::string& str, const validate::StringRules& string_rules) {
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
        char_len = Protobuf::UTF8FirstLetterNumBytes(codepoint_ptr, byte_len);
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
      ENVOY_LOG_MISC(MYLOGLEV, "pgv::StringRules::pattern '{}' found, not handling yet",
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
      ENVOY_LOG_MISC(MYLOGLEV, "pgv::StringRules::in|not_in rule found, not handling yet");
    }
    if (string_rules.has_email()) {
      ENVOY_LOG_MISC(MYLOGLEV, "pgv::StringRules::email rule found, not handling yet");
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
      ENVOY_LOG_MISC(MYLOGLEV, "pgv::StringRules::uri rule found, not handling yet");
    }
    if (string_rules.has_uri_ref()) {
      ENVOY_LOG_MISC(MYLOGLEV, "pgv::StringRules::uri_ref rule found, not handling yet");
    }
    if (string_rules.has_len()) {
      ENVOY_LOG_MISC(MYLOGLEV, "pgv::StringRules::len rule found, not handling yet");
    }
    if (string_rules.has_len_bytes() && str.length() != string_rules.len_bytes()) {

      if (str.length() < string_rules.len_bytes()) {
        str += std::string(string_rules.len_bytes() - str.length(), 'a');
      } else {
        str = str.substr(0, string_rules.len_bytes());
      }
    }

    if (string_rules.has_address()) {
      ENVOY_LOG_MISC(MYLOGLEV, "pgv::StringRules::address rule found, not handling yet");
    }
    if (string_rules.has_uuid()) {
      ENVOY_LOG_MISC(MYLOGLEV, "pgv::StringRules::uuid rule found, not handling yet");
    }
    {
      std::string::size_type found_at;
      if (string_rules.has_not_contains() &&
          ((found_at = str.find_first_of(string_rules.not_contains(), 0))) != std::string::npos) {
        str[found_at] += 1; // just increase the first character's byte. Yes, quite ugly.
      }
    }
    if (string_rules.has_well_known_regex()) {
      ENVOY_LOG_MISC(MYLOGLEV, "pgv::StringRules::well_known_regex rule found, not handling yet");
    }
  }

  static void handle_any_rules(Protobuf::Message* msg, const validate::AnyRules& any_rules) {
    ProtobufWkt::Any* any = dynamic_cast<ProtobufWkt::Any*>(msg);
    if (any_rules.required()) {
      ASSERT(any);
    }
    if (any_rules.in_size() > 0 || any_rules.not_in_size() > 0) {
      ENVOY_LOG_MISC(MYLOGLEV, "pgv::AnyRules::in|not_in rule found, not handling yet");
    }
  }

  void handle_message_typed_field(Protobuf::Message& msg, const Protobuf::FieldDescriptor& field,
                                  const Protobuf::Reflection* reflection,
                                  const validate::FieldRules& rules, const bool force) {

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
      if (force || reflection->HasField(msg, &field) ||
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
          handle_any_rules(value, rules.any());
          break;
        }
        default:
          break;
        }
      }
    }
  }

  // Handle all validation rules for intrinsic types like int, uint and string.
  // Messages are more complicated to handle and can not be handled here.
  template <typename T, auto FIELDGETTER, auto FIELDSETTER, auto REPGETTER, auto REPSETTER,
            auto FIELDADDER, auto RULEGETTER,
            auto TYPEHANDLER = &handle_numeric_rules<
                T, typename std::result_of<decltype(RULEGETTER)(validate::FieldRules)>::type>>
  void handle_intrinsic_typed_field(Protobuf::Message& msg, const Protobuf::FieldDescriptor& field,
                                    const Protobuf::Reflection* reflection,
                                    const validate::FieldRules& rules, const bool force) {

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

  void onField(Protobuf::Message& msg, const Protobuf::FieldDescriptor& field) override {
    onField(msg, field, false);
  }

  void onField(Protobuf::Message& msg, const Protobuf::FieldDescriptor& field, const bool force) {
    const Protobuf::Reflection* reflection = msg.GetReflection();

    if (!field.options().HasExtension(validate::rules) && !force) {
      return;
    }
    const validate::FieldRules& rules = field.options().GetExtension(validate::rules);
    if (rules.message().has_skip() && rules.message().skip()) {
      return;
    }

    switch (field.cpp_type()) {
    case Protobuf::FieldDescriptor::CPPTYPE_INT32: {
      handle_intrinsic_typed_field<
          std::int32_t, &Protobuf::Reflection::GetInt32, &Protobuf::Reflection::SetInt32,
          &Protobuf::Reflection::GetRepeatedInt32, &Protobuf::Reflection::SetRepeatedInt32,
          &Protobuf::Reflection::AddInt32, &validate::FieldRules::int32>(msg, field, reflection,
                                                                         rules, force);
      break;
    }
    case Protobuf::FieldDescriptor::CPPTYPE_INT64: {
      handle_intrinsic_typed_field<
          std::int64_t, &Protobuf::Reflection::GetInt64, &Protobuf::Reflection::SetInt64,
          &Protobuf::Reflection::GetRepeatedInt64, &Protobuf::Reflection::SetRepeatedInt64,
          &Protobuf::Reflection::AddInt64, &validate::FieldRules::int64>(msg, field, reflection,
                                                                         rules, force);
      break;
    }
    case Protobuf::FieldDescriptor::CPPTYPE_UINT32: {
      handle_intrinsic_typed_field<
          std::uint32_t, &Protobuf::Reflection::GetUInt32, &Protobuf::Reflection::SetUInt32,
          &Protobuf::Reflection::GetRepeatedUInt32, &Protobuf::Reflection::SetRepeatedUInt32,
          &Protobuf::Reflection::AddUInt32, &validate::FieldRules::uint32>(msg, field, reflection,
                                                                           rules, force);
      break;
    }
    case Protobuf::FieldDescriptor::CPPTYPE_UINT64: {
      handle_intrinsic_typed_field<
          std::uint64_t, &Protobuf::Reflection::GetUInt64, &Protobuf::Reflection::SetUInt64,
          &Protobuf::Reflection::GetRepeatedUInt64, &Protobuf::Reflection::SetRepeatedUInt64,
          &Protobuf::Reflection::AddUInt64, &validate::FieldRules::uint64>(msg, field, reflection,
                                                                           rules, force);
      break;
    }
    case Protobuf::FieldDescriptor::CPPTYPE_DOUBLE: {
      handle_intrinsic_typed_field<
          double, &Protobuf::Reflection::GetDouble, &Protobuf::Reflection::SetDouble,
          &Protobuf::Reflection::GetRepeatedDouble, &Protobuf::Reflection::SetRepeatedDouble,
          &Protobuf::Reflection::AddDouble, &validate::FieldRules::double_>(msg, field, reflection,
                                                                            rules, force);
      break;
    }
    case Protobuf::FieldDescriptor::CPPTYPE_FLOAT: {
      handle_intrinsic_typed_field<
          float, &Protobuf::Reflection::GetFloat, &Protobuf::Reflection::SetFloat,
          &Protobuf::Reflection::GetRepeatedFloat, &Protobuf::Reflection::SetRepeatedFloat,
          &Protobuf::Reflection::AddFloat, &validate::FieldRules::float_>(msg, field, reflection,
                                                                          rules, force);
      break;
    }
    case Protobuf::FieldDescriptor::CPPTYPE_BOOL:
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_ENUM:
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_STRING: {
      handle_intrinsic_typed_field<
          std::string, &Protobuf::Reflection::GetString, &Protobuf::Reflection::SetString,
          &Protobuf::Reflection::GetRepeatedString, &Protobuf::Reflection::SetRepeatedString,
          &Protobuf::Reflection::AddString, &validate::FieldRules::string, &handle_string_rules>(
          msg, field, reflection, rules, force);
      break;
    }
    case Protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
      handle_message_typed_field(msg, field, reflection, rules, force);
      break;
    }
    default:
      break;
    }
  }

  void onMessage(Protobuf::Message& msg, absl::Span<const Protobuf::Message* const>,
                 bool) override {
    const Protobuf::Reflection* reflection = msg.GetReflection();
    const Protobuf::Descriptor* descriptor = msg.GetDescriptor();
    for (int oneof_index = 0; oneof_index < descriptor->oneof_decl_count(); ++oneof_index) {
      const Protobuf::OneofDescriptor* oneof_desc = descriptor->oneof_decl(oneof_index);
      if (oneof_desc->options().HasExtension(validate::required) &&
          oneof_desc->options().GetExtension(validate::required) &&
          !reflection->HasOneof(msg, descriptor->oneof_decl(oneof_index))) {
        // No required member in one of set.
        for (int index = 0; index < oneof_desc->field_count(); ++index) {
          onField(msg, *oneof_desc->field(index), true);
          // Check if for the above field an entry could be created and quit the inner loop if so.
          // It might not be possible, when the datatype is not supported (yet).
          if (reflection->HasOneof(msg, descriptor->oneof_decl(oneof_index))) {
            break;
          }
        }
      }
    }
  }

private:
  Mutator mutator_;
};

DEFINE_PROTO_FUZZER(const test::extensions::filters::network::FilterFuzzTestCase& input) {
  TestDeprecatedV2Api _deprecated_v2_api;
  ABSL_ATTRIBUTE_UNUSED static PostProcessorRegistration reg = {
      [](test::extensions::filters::network::FilterFuzzTestCase* input, unsigned int seed) {
        // This post-processor mutation is applied only when libprotobuf-mutator
        // calls mutate on an input, and *not* during fuzz target execution.
        // Replaying a corpus through the fuzzer will not be affected by the
        // post-processor mutation.

        // TODO(jianwendong): After extending to cover all the filters, we can use
        // `Registry::FactoryRegistry<
        // Server::Configuration::NamedNetworkFilterConfigFactory>::registeredNames()`
        // to get all the filter names instead of calling `UberFilterFuzzer::filter_names()`.
        static const auto filter_names = UberFilterFuzzer::filterNames();
        static const auto factories = Registry::FactoryRegistry<
            Server::Configuration::NamedNetworkFilterConfigFactory>::factories();
        // Choose a valid filter name.
        if (std::find(filter_names.begin(), filter_names.end(), input->config().name()) ==
            std::end(filter_names)) {
          absl::string_view filter_name = filter_names[seed % filter_names.size()];
          input->mutable_config()->set_name(std::string(filter_name));
        }
        // Set the corresponding type_url for Any.
        auto& factory = factories.at(input->config().name());
        input->mutable_config()->mutable_typed_config()->set_type_url(
            absl::StrCat("type.googleapis.com/",
                         factory->createEmptyConfigProto()->GetDescriptor()->full_name()));

        GenerateValidMessage generator(seed);
        ProtobufMessage::traverseMessage(generator, *input, true);
      }};

  try {
    TestUtility::validate(input);
    // Check the filter's name in case some filters are not supported yet.
    static const auto filter_names = UberFilterFuzzer::filterNames();
    // TODO(jianwendong): remove this if block after covering all the filters.
    if (std::find(filter_names.begin(), filter_names.end(), input.config().name()) ==
        std::end(filter_names)) {
      ENVOY_LOG_MISC(debug, "Test case with unsupported filter type: {}", input.config().name());
      return;
    }
    static UberFilterFuzzer fuzzer;
    fuzzer.fuzz(input.config(), input.actions());
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
  }
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
