#include "test/server/admin/admin_instance.h"

namespace Envoy {
namespace Server {

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminInstanceTest, Runtime) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  Runtime::MockSnapshot snapshot;
  Runtime::MockLoader loader;
  auto layer1 = std::make_unique<NiceMock<Runtime::MockOverrideLayer>>();
  auto layer2 = std::make_unique<NiceMock<Runtime::MockOverrideLayer>>();
  Runtime::Snapshot::EntryMap entries2{{"string_key", {"override", {}, {}, {}, {}}},
                                       {"extra_key", {"bar", {}, {}, {}, {}}}};
  Runtime::Snapshot::EntryMap entries1{{"string_key", {"foo", {}, {}, {}, {}}},
                                       {"int_key", {"1", 1, {}, {}, {}}},
                                       {"other_key", {"bar", {}, {}, {}, {}}}};

  ON_CALL(*layer1, name()).WillByDefault(testing::ReturnRefOfCopy(std::string{"layer1"}));
  ON_CALL(*layer1, values()).WillByDefault(testing::ReturnRef(entries1));
  ON_CALL(*layer2, name()).WillByDefault(testing::ReturnRefOfCopy(std::string{"layer2"}));
  ON_CALL(*layer2, values()).WillByDefault(testing::ReturnRef(entries2));

  std::vector<Runtime::Snapshot::OverrideLayerConstPtr> layers;
  layers.push_back(std::move(layer1));
  layers.push_back(std::move(layer2));
  EXPECT_CALL(snapshot, getLayers()).WillRepeatedly(testing::ReturnRef(layers));

  const std::string expected_json = R"EOF({
    "layers": [
        "layer1",
        "layer2"
    ],
    "entries": {
        "extra_key": {
            "layer_values": [
                "",
                "bar"
            ],
            "final_value": "bar"
        },
        "int_key": {
            "layer_values": [
                "1",
                ""
            ],
            "final_value": "1"
        },
        "other_key": {
            "layer_values": [
                "bar",
                ""
            ],
            "final_value": "bar"
        },
        "string_key": {
            "layer_values": [
                "foo",
                "override"
            ],
            "final_value": "override"
        }
    }
})EOF";

  EXPECT_CALL(loader, snapshot()).WillRepeatedly(testing::ReturnPointee(&snapshot));
  EXPECT_CALL(server_, runtime()).WillRepeatedly(testing::ReturnPointee(&loader));
  EXPECT_EQ(Http::Code::OK, getCallback("/runtime", header_map, response));
  EXPECT_THAT(expected_json, JsonStringEq(response.toString()));
}

TEST_P(AdminInstanceTest, RuntimeModify) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  Runtime::MockLoader loader;
  EXPECT_CALL(server_, runtime()).WillRepeatedly(testing::ReturnPointee(&loader));

  absl::node_hash_map<std::string, std::string> overrides;
  overrides["foo"] = "bar";
  overrides["x"] = "42";
  overrides["nothing"] = "";
  EXPECT_CALL(loader, mergeValues(overrides));
  EXPECT_EQ(Http::Code::OK,
            postCallback("/runtime_modify?foo=bar&x=42&nothing=", header_map, response));
  EXPECT_EQ("OK\n", response.toString());
}

TEST_P(AdminInstanceTest, RuntimeModifyParamsInBody) {
  Runtime::MockLoader loader;
  EXPECT_CALL(server_, runtime()).WillRepeatedly(testing::ReturnPointee(&loader));

  const std::string key = "routing.traffic_shift.foo";
  const std::string value = "numerator: 1\ndenominator: TEN_THOUSAND\n";
  const absl::node_hash_map<std::string, std::string> overrides = {{key, value}};
  EXPECT_CALL(loader, mergeValues(overrides));

  const std::string body = fmt::format("{}={}", key, value);
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, runCallback("/runtime_modify", header_map, response, "POST", body));
  EXPECT_EQ("OK\n", response.toString());
}

TEST_P(AdminInstanceTest, RuntimeModifyNoArguments) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  EXPECT_EQ(Http::Code::BadRequest, postCallback("/runtime_modify", header_map, response));
  EXPECT_TRUE(absl::StartsWith(response.toString(), "usage:"));
}

} // namespace Server
} // namespace Envoy
