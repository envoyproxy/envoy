#pragma once

#include "test/fuzz/mutable_visitor.h"
#include "test/fuzz/random.h"

#include "src/libfuzzer/libfuzzer_mutator.h"
#include "validate/validate.h"
#include "validate/validate.pb.h"

namespace Envoy {
namespace ProtobufMessage {

template <typename T, typename R> static bool handleNumericRules(T& number, const R& number_rules);

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

  static AnyMap getDefaultAnyMap();

  class Mutator : public protobuf_mutator::libfuzzer::Mutator {
  public:
    using protobuf_mutator::libfuzzer::Mutator::Mutator;

    using protobuf_mutator::libfuzzer::Mutator::MutateString;
  };
  ValidatedInputGenerator(unsigned int seed, AnyMap&& default_any_map = getDefaultAnyMap(),
                          unsigned int max_depth = 0);

  void handleAnyRules(Protobuf::Message* msg, const validate::AnyRules& any_rules,
                      const absl::Span<const Protobuf::Message* const>& parents);

  void handleMessageTypedField(Protobuf::Message& msg, const Protobuf::FieldDescriptor& field,
                               const Protobuf::Reflection* reflection,
                               const validate::FieldRules& rules,
                               const absl::Span<const Protobuf::Message* const>& parents,
                               bool force_create, bool cut_off);

  // Handle all validation rules for intrinsic types like int, uint and string.
  // Messages are more complicated to handle and can not be handled here.
  template <typename T, auto FIELDGETTER, auto FIELDSETTER, auto REPGETTER, auto REPSETTER,
            auto FIELDADDER, auto RULEGETTER,
            auto TYPEHANDLER = &handleNumericRules<
                T, typename std::invoke_result<decltype(RULEGETTER), validate::FieldRules>::type>>
  void handleIntrinsicTypedField(Protobuf::Message& msg, const Protobuf::FieldDescriptor& field,
                                 const Protobuf::Reflection* reflection,
                                 const validate::FieldRules& rules, bool force);

  void onField(Protobuf::Message& msg, const Protobuf::FieldDescriptor& field,
               const absl::Span<const Protobuf::Message* const> parents) override;

  void onField(Protobuf::Message& msg, const Protobuf::FieldDescriptor& field,
               const absl::Span<const Protobuf::Message* const> parents, bool force_create,
               bool cut_off);

  void onEnterMessage(Protobuf::Message& msg, absl::Span<const Protobuf::Message* const> parents,
                      bool, absl::string_view field_name) override;

  void onLeaveMessage(Protobuf::Message&, absl::Span<const Protobuf::Message* const>, bool,
                      absl::string_view) override;

private:
  Mutator mutator_;
  Random::PsuedoRandomGenerator64 random_;
  std::deque<absl::string_view> message_path_;
  unsigned int current_depth_;

  const unsigned int max_depth_;
  const AnyMap any_map_;
};
} // namespace ProtobufMessage
} // namespace Envoy
