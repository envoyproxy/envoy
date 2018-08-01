#include <string>

#include "envoy/stats/stats.h"

#include "extensions/filters/http/dynamo/dynamo_utility.h"

#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

TEST(DynamoUtility, PartitionIdStatString) {
  Stats::StatsOptionsImpl stats_options;
  stats_options.max_obj_name_length_ = 60;

  {
    std::string stat_prefix = "stat.prefix.";
    std::string table_name = "locations";
    std::string operation = "GetItem";
    std::string partition_id = "6235c781-1d0d-47a3-a4ea-eec04c5883ca";
    std::string partition_stat_string = Utility::buildPartitionStatString(
        stat_prefix, table_name, operation, partition_id, stats_options);
    std::string expected_stat_string =
        "stat.prefix.table.locations.capacity.GetItem.__partition_id=c5883ca";
    EXPECT_EQ(expected_stat_string, partition_stat_string);
    EXPECT_TRUE(partition_stat_string.size() <= stats_options.maxNameLength());
  }

  {
    std::string stat_prefix = "http.egress_dynamodb_iad.dynamodb.";
    std::string table_name = "locations-sandbox-partition-test-iad-mytest-really-long-name";
    std::string operation = "GetItem";
    std::string partition_id = "6235c781-1d0d-47a3-a4ea-eec04c5883ca";

    std::string partition_stat_string = Utility::buildPartitionStatString(
        stat_prefix, table_name, operation, partition_id, stats_options);
    std::string expected_stat_string = "http.egress_dynamodb_iad.dynamodb.table.locations-sandbox-"
                                       "partition-test-iad-mytest-rea.capacity.GetItem.__partition_"
                                       "id=c5883ca";
    EXPECT_EQ(expected_stat_string, partition_stat_string);
    EXPECT_TRUE(partition_stat_string.size() <= stats_options.maxNameLength());
  }
  {
    std::string stat_prefix = "http.egress_dynamodb_iad.dynamodb.";
    std::string table_name = "locations-sandbox-partition-test-iad-mytest-rea";
    std::string operation = "GetItem";
    std::string partition_id = "6235c781-1d0d-47a3-a4ea-eec04c5883ca";

    std::string partition_stat_string = Utility::buildPartitionStatString(
        stat_prefix, table_name, operation, partition_id, stats_options);
    std::string expected_stat_string = "http.egress_dynamodb_iad.dynamodb.table.locations-sandbox-"
                                       "partition-test-iad-mytest-rea.capacity.GetItem.__partition_"
                                       "id=c5883ca";

    EXPECT_EQ(expected_stat_string, partition_stat_string);
    EXPECT_TRUE(partition_stat_string.size() <= stats_options.maxNameLength());
  }
}

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
