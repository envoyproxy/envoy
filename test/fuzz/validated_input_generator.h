#pragma once

#include "test/fuzz/mutable_visitor.h"
#include "test/fuzz/random.h"

#include "src/libfuzzer/libfuzzer_mutator.h"
#include "validate/validate.h"
#include "validate/validate.pb.h"

namespace Envoy {
namespace ProtobufMessage {

template <typename T, typename R>
static bool handle_numeric_rules(T& number, const R& number_rules);

class ValidatedInputGenerator : public ProtobufMessage::ProtoVisitor, private pgv::BaseValidator {
public:
  static const std::string kAny;

  using SimpleProtoFactory = std::function<std::unique_ptr<Protobuf::Message>()>;
  using TypeUrlAndFactory =
      std::pair<std::string /* type_url */, SimpleProtoFactory /* simple_prototype_factory */>;
  using ListOfTypeUrlAndFactory = std::vector<TypeUrlAndFactory>;
  using FieldToTypeUrls =
      std::map<std::string /* field */, ListOfTypeUrlAndFactory /* type_url_to_factories */>;
  using AnyMap = std::map<std::string /* class name */, FieldToTypeUrls>;

  static AnyMap get_default_any_map();

  class Mutator : public protobuf_mutator::libfuzzer::Mutator {
  public:
    using protobuf_mutator::libfuzzer::Mutator::Mutator;

    using protobuf_mutator::libfuzzer::Mutator::MutateString;
  };
  ValidatedInputGenerator(unsigned int seed, AnyMap&& default_any_map = get_default_any_map());

  void handle_any_rules(Protobuf::Message* msg, const validate::AnyRules& any_rules,
                        const absl::Span<const Protobuf::Message* const>& parents);

  void handle_message_typed_field(Protobuf::Message& msg, const Protobuf::FieldDescriptor& field,
                                  const Protobuf::Reflection* reflection,
                                  const validate::FieldRules& rules,
                                  const absl::Span<const Protobuf::Message* const>& parents,
                                  const bool force_create);

  // Handle all validation rules for intrinsic types like int, uint and string.
  // Messages are more complicated to handle and can not be handled here.
  template <typename T, auto FIELDGETTER, auto FIELDSETTER, auto REPGETTER, auto REPSETTER,
            auto FIELDADDER, auto RULEGETTER,
            auto TYPEHANDLER = &handle_numeric_rules<
                T, typename std::result_of<decltype(RULEGETTER)(validate::FieldRules)>::type>>
  void handle_intrinsic_typed_field(Protobuf::Message& msg, const Protobuf::FieldDescriptor& field,
                                    const Protobuf::Reflection* reflection,
                                    const validate::FieldRules& rules, const bool force);

  void onField(Protobuf::Message& msg, const Protobuf::FieldDescriptor& field,
               const absl::Span<const Protobuf::Message* const> parents) override;

  void onField(Protobuf::Message& msg, const Protobuf::FieldDescriptor& field,
               const absl::Span<const Protobuf::Message* const> parents, const bool force_create);

  void onEnterMessage(Protobuf::Message& msg, absl::Span<const Protobuf::Message* const> parents,
                      bool, absl::string_view const& field_name) override;

  void onLeaveMessage(Protobuf::Message&, absl::Span<const Protobuf::Message* const>, bool,
                      absl::string_view const&) override;

private:
  Mutator mutator_;
  Random::PsuedoRandomGenerator64 random_;
  std::deque<absl::string_view> message_path_;

  const AnyMap any_map;
};
} // namespace ProtobufMessage
} // namespace Envoy
