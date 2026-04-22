#include "source/common/common/assert.h"
#include "source/common/grpc/common.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "test/extensions/filters/http/proto_api_scrubber/filter_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "xds/type/matcher/v3/cel.pb.h"
#include "xds/type/matcher/v3/http_inputs.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {

using envoy::extensions::filters::http::proto_api_scrubber::ProtoApiScrubberFuzzInput;
using envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig;

// Creates a valid FileDescriptorSet in memory to bootstrap the filter.
std::string createFuzzDescriptorSet() {
  Envoy::Protobuf::FileDescriptorSet descriptor_set;

  auto* any_file = descriptor_set.add_file();
  any_file->set_name("google/"
                     "protobuf/any.proto");
  any_file->set_package("google.protobuf");
  any_file->set_syntax("proto3");

  auto* any_msg = any_file->add_message_type();
  any_msg->set_name("Any");

  auto* type_url = any_msg->add_field();
  type_url->set_name("type_url");
  type_url->set_number(1);
  type_url->set_type(Protobuf::FieldDescriptorProto::TYPE_STRING);

  auto* value = any_msg->add_field();
  value->set_name("value");
  value->set_number(2);
  value->set_type(Protobuf::FieldDescriptorProto::TYPE_BYTES);

  auto* file_proto = descriptor_set.add_file();
  file_proto->set_name("fuzz.proto");
  file_proto->set_package("fuzz");
  file_proto->set_syntax("proto3");
  file_proto->add_dependency("google/"
                             "protobuf/any.proto");

  auto* enum_type = file_proto->add_enum_type();
  enum_type->set_name("FuzzEnum");
  auto* e1 = enum_type->add_value();
  e1->set_name("UNKNOWN");
  e1->set_number(0);
  auto* e2 = enum_type->add_value();
  e2->set_name("VAL1");
  e2->set_number(1);

  auto* map_entry = file_proto->add_message_type();
  map_entry->set_name("MapValEntry");
  map_entry->mutable_options()->set_map_entry(true);
  auto* k = map_entry->add_field();
  k->set_name("key");
  k->set_number(1);
  k->set_type(Protobuf::FieldDescriptorProto::TYPE_STRING);
  auto* v = map_entry->add_field();
  v->set_name("value");
  v->set_number(2);
  v->set_type(Protobuf::FieldDescriptorProto::TYPE_STRING);

  auto* msg = file_proto->add_message_type();
  msg->set_name("FuzzMessage");

  auto* f1 = msg->add_field();
  f1->set_name("data");
  f1->set_number(1);
  f1->set_type(Protobuf::FieldDescriptorProto::TYPE_STRING);

  auto* f2 = msg->add_field();
  f2->set_name("nested");
  f2->set_number(2);
  f2->set_type(Protobuf::FieldDescriptorProto::TYPE_MESSAGE);
  f2->set_type_name(".fuzz.FuzzMessage");

  auto* f3 = msg->add_field();
  f3->set_name("numbers");
  f3->set_number(3);
  f3->set_type(Protobuf::FieldDescriptorProto::TYPE_INT32);
  f3->set_label(Protobuf::FieldDescriptorProto::LABEL_REPEATED);

  auto* f4 = msg->add_field();
  f4->set_name("enum_val");
  f4->set_number(4);
  f4->set_type(Protobuf::FieldDescriptorProto::TYPE_ENUM);
  f4->set_type_name(".fuzz.FuzzEnum");

  auto* f5 = msg->add_field();
  f5->set_name("map_val");
  f5->set_number(5);
  f5->set_type(Protobuf::FieldDescriptorProto::TYPE_MESSAGE);
  f5->set_label(Protobuf::FieldDescriptorProto::LABEL_REPEATED);
  f5->set_type_name(".fuzz.MapValEntry");

  auto* oneof_decl = msg->add_oneof_decl();
  oneof_decl->set_name("choice");

  auto* f6 = msg->add_field();
  f6->set_name("choice_a");
  f6->set_number(6);
  f6->set_type(Protobuf::FieldDescriptorProto::TYPE_STRING);
  f6->set_oneof_index(0);

  auto* f7 = msg->add_field();
  f7->set_name("choice_b");
  f7->set_number(7);
  f7->set_type(Protobuf::FieldDescriptorProto::TYPE_INT32);
  f7->set_oneof_index(0);

  auto* f8 = msg->add_field();
  f8->set_name("any_val");
  f8->set_number(8);
  f8->set_type(Protobuf::FieldDescriptorProto::TYPE_MESSAGE);
  f8->set_type_name(".google.protobuf.Any");

  auto* service = file_proto->add_service();
  service->set_name("FuzzService");
  auto* method = service->add_method();
  method->set_name("FuzzMethod");
  method->set_input_type(".fuzz.FuzzMessage");
  method->set_output_type(".fuzz.FuzzMessage");

  std::string bytes;
  descriptor_set.SerializeToString(&bytes);
  return bytes;
}

ProtoApiScrubberConfig createFuzzConfig() {
  ProtoApiScrubberConfig config;
  config.set_filtering_mode(ProtoApiScrubberConfig::OVERRIDE);

  // Inline the descriptor bytes to avoid file system dependency.
  *config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
      createFuzzDescriptorSet();

  auto& method_rules = (*config.mutable_restrictions()
                             ->mutable_method_restrictions())["/fuzz.FuzzService/FuzzMethod"];

  xds::type::matcher::v3::Matcher matcher;
  auto* entry = matcher.mutable_matcher_list()->add_matchers();

  auto* cel_matcher = entry->mutable_predicate()
                          ->mutable_single_predicate()
                          ->mutable_custom_match()
                          ->mutable_typed_config();
  xds::type::matcher::v3::CelMatcher cel;
  cel.mutable_expr_match()
      ->mutable_parsed_expr()
      ->mutable_expr()
      ->mutable_const_expr()
      ->set_bool_value(true);
  cel_matcher->PackFrom(cel);

  auto* action_config = entry->mutable_on_match()->mutable_action()->mutable_typed_config();
  envoy::extensions::filters::http::proto_api_scrubber::v3::RemoveFieldAction remove;
  action_config->PackFrom(remove);
  entry->mutable_on_match()->mutable_action()->set_name("remove_field");

  auto* req_rules = method_rules.mutable_request_field_restrictions();
  auto* res_rules = method_rules.mutable_response_field_restrictions();

  (*req_rules)["data"].mutable_matcher()->CopyFrom(matcher);
  (*req_rules)["nested.data"].mutable_matcher()->CopyFrom(matcher);
  (*req_rules)["map_val.value"].mutable_matcher()->CopyFrom(matcher);
  (*req_rules)["choice_a"].mutable_matcher()->CopyFrom(matcher);

  (*res_rules)["data"].mutable_matcher()->CopyFrom(matcher);

  // We intentionally do NOT add a removal rule for "any_val" (Field 8).
  // This causes the scrubber to return kPartial for the Any field, triggering the
  // ScanAnyField logic which attempts to parse the inner content based on type_url.
  // This maximizes coverage of the custom Any parser.

  return config;
}

DEFINE_PROTO_FUZZER(const ProtoApiScrubberFuzzInput& input) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

  // Set buffer limits high to avoid triggering resource exhausted errors prematurely.
  ON_CALL(decoder_callbacks, bufferLimit()).WillByDefault(testing::Return(1024 * 1024));
  ON_CALL(encoder_callbacks, bufferLimit()).WillByDefault(testing::Return(1024 * 1024));

  auto config_proto = createFuzzConfig();
  auto config_or_error = ProtoApiScrubberFilterConfig::create(config_proto, factory_context);

  if (!config_or_error.ok()) {
    return;
  }
  auto config = config_or_error.value();

  auto filter = std::make_unique<ProtoApiScrubberFilter>(*config);
  filter->setDecoderFilterCallbacks(decoder_callbacks);
  filter->setEncoderFilterCallbacks(encoder_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/fuzz.FuzzService/FuzzMethod"},
                                                 {"content-type", "application/grpc"}};

  if (filter->decodeHeaders(request_headers, false) == Http::FilterHeadersStatus::Continue) {
    for (const auto& chunk : input.request_data()) {
      Buffer::OwnedImpl buffer(chunk);
      // Ignore status; the primary goal is ensuring no crashes during parsing.
      filter->decodeData(buffer, false);
    }
    Buffer::OwnedImpl empty;
    filter->decodeData(empty, true);
  }

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"content-type", "application/grpc"}};

  if (filter->encodeHeaders(response_headers, false) == Http::FilterHeadersStatus::Continue) {
    for (const auto& chunk : input.response_data()) {
      Buffer::OwnedImpl buffer(chunk);
      filter->encodeData(buffer, false);
    }
    Buffer::OwnedImpl empty;
    filter->encodeData(empty, true);
  }
}

} // namespace
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
