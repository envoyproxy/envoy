#include <vector>

#include "envoy/type/matcher/v3/regex.pb.h"

#include "source/common/common/regex.h"
#include "source/extensions/filters/http/orca_reporter/cgroup_stats.h"

#include "test/extensions/filters/http/orca_reporter/fake_cgroup.h"
#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OrcaReporter {
namespace {

using StatusHelpers::HasStatus;
using StatusHelpers::HasStatusCode;
using StatusHelpers::IsOkAndHolds;
using testing::HasSubstr;

class ResolvePathTemplateTest : public testing::Test {
protected:
  EnvLookup envLookup() {
    return [this](const std::string& name) -> std::optional<std::string> {
      auto it = env_.find(name);
      if (it == env_.end()) {
        return std::nullopt;
      }
      return it->second;
    };
  }

  void addRewrite(const std::string& regex, const std::string& substitution) {
    envoy::type::matcher::v3::RegexMatchAndSubstitute proto;
    proto.mutable_pattern()->set_regex(regex);
    proto.set_substitution(substitution);
    rewrites_.push_back(Matcher::RegexReplace::create(regex_engine_, proto).value());
  }

  absl::StatusOr<std::string> resolve(absl::string_view path_template) {
    return resolvePathTemplate(path_template, rewrites_, envLookup());
  }

  Regex::GoogleReEngine regex_engine_;
  std::vector<Matcher::RegexReplace> rewrites_;
  absl::flat_hash_map<std::string, std::string> env_;
};

TEST_F(ResolvePathTemplateTest, NoPlaceholder) {
  EXPECT_THAT(resolve("/sys/fs/cgroup"), IsOkAndHolds("/sys/fs/cgroup"));
}

TEST_F(ResolvePathTemplateTest, Placeholders) {
  env_["POD_UID"] = "1a2b-3c4d";
  env_["_QOS2"] = "burstable";
  EXPECT_THAT(resolve("/cg/kubepods/{_QOS2}/pod{POD_UID}/{POD_UID}"),
              IsOkAndHolds("/cg/kubepods/burstable/pod1a2b-3c4d/1a2b-3c4d"));
}

TEST_F(ResolvePathTemplateTest, RewritesOnlyApplyToValues) {
  env_["POD_UID"] = "1a2b-3c4d";
  addRewrite("-", "_");
  EXPECT_THAT(resolve("/cg/kubepods-burstable-pod{POD_UID}.slice"),
              IsOkAndHolds("/cg/kubepods-burstable-pod1a2b_3c4d.slice"));
}

TEST_F(ResolvePathTemplateTest, RewritesApplyInOrder) {
  env_["NAME"] = "abc";
  addRewrite("a", "b");
  addRewrite("b", "c");
  EXPECT_THAT(resolve("/{NAME}"), IsOkAndHolds("/ccc"));
}

TEST_F(ResolvePathTemplateTest, StrayClosingBraceIsLiteral) {
  EXPECT_THAT(resolve("/a}b"), IsOkAndHolds("/a}b"));
}

TEST_F(ResolvePathTemplateTest, UnsetVariable) {
  EXPECT_THAT(resolve("/pod{POD_UID}"), HasStatus(absl::StatusCode::kInvalidArgument,
                                                  HasSubstr("environment variable 'POD_UID'")));
}

TEST_F(ResolvePathTemplateTest, EmptyVariable) {
  env_["POD_UID"] = "";
  EXPECT_THAT(resolve("/pod{POD_UID}"),
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("not set or empty")));
}

TEST_F(ResolvePathTemplateTest, UnterminatedPlaceholder) {
  EXPECT_THAT(resolve("/pod{POD_UID"),
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("unterminated")));
}

TEST_F(ResolvePathTemplateTest, InvalidPlaceholderNames) {
  for (const std::string path : {"/{}", "/{1A}", "/{A-B}", "/{A{B}"}) {
    EXPECT_THAT(resolve(path),
                HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("invalid placeholder")))
        << path;
  }
}

TEST(ProcessEnvLookupTest, ReadsProcessEnvironment) {
  TestEnvironment::setEnvVar("ORCA_REPORTER_TEST_VAR", "value", 1);
  EXPECT_EQ(processEnvLookup("ORCA_REPORTER_TEST_VAR"), "value");
  TestEnvironment::unsetEnvVar("ORCA_REPORTER_TEST_VAR");
  EXPECT_EQ(processEnvLookup("ORCA_REPORTER_TEST_VAR"), std::nullopt);
}

TEST(CgroupParserTest, CpuStatUsageUsec) {
  EXPECT_THAT(parseCpuStatUsageUsec("usage_usec 12345\nuser_usec 1\n"), IsOkAndHolds(12345U));
  EXPECT_THAT(parseCpuStatUsageUsec("user_usec 1\nusage_usec 7\n"), IsOkAndHolds(7U));
  EXPECT_THAT(parseCpuStatUsageUsec("user_usec 1\n"), HasStatusCode(absl::StatusCode::kNotFound));
  EXPECT_THAT(parseCpuStatUsageUsec("usage_usec abc\n"),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
}

TEST(CgroupParserTest, CpuMax) {
  EXPECT_THAT(parseCpuMax("max 100000\n"), IsOkAndHolds(std::nullopt));
  EXPECT_THAT(parseCpuMax("200000 100000\n"), IsOkAndHolds(2.0));
  EXPECT_THAT(parseCpuMax("50000 100000"), IsOkAndHolds(0.5));
  for (const std::string value : {"", "100000", "1 2 3", "abc 100000", "100 0", "0 100000"}) {
    EXPECT_THAT(parseCpuMax(value), HasStatusCode(absl::StatusCode::kInvalidArgument)) << value;
  }
}

TEST(CgroupParserTest, SingleValue) {
  EXPECT_THAT(parseSingleValue("123\n"), IsOkAndHolds(123U));
  EXPECT_THAT(parseSingleValue("abc"), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(parseSingleValue("-1"), HasStatusCode(absl::StatusCode::kInvalidArgument));
}

TEST(CgroupParserTest, MemoryMax) {
  EXPECT_THAT(parseMemoryMax("max\n"), IsOkAndHolds(std::nullopt));
  EXPECT_THAT(parseMemoryMax("1024\n"), IsOkAndHolds(1024U));
  EXPECT_THAT(parseMemoryMax("0\n"), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(parseMemoryMax("abc"), HasStatusCode(absl::StatusCode::kInvalidArgument));
}

TEST(CgroupParserTest, MemoryStatInactiveFile) {
  EXPECT_THAT(parseMemoryStatInactiveFile("anon 1\ninactive_file 42\n"), IsOkAndHolds(42U));
  EXPECT_THAT(parseMemoryStatInactiveFile("anon 1\n"), HasStatusCode(absl::StatusCode::kNotFound));
}

class CgroupReaderTest : public testing::Test {
protected:
  CgroupReaderTest()
      : api_(Api::createApiForTest()), reader_(api_->fileSystem(), cgroup_.directory()) {}

  FakeCgroup cgroup_{"cgroup_reader"};
  Api::ApiPtr api_;
  CgroupReader reader_;
};

TEST_F(CgroupReaderTest, ReadsInterfaceFiles) {
  cgroup_.setCpuUsage(5000);
  cgroup_.setCpuMax("150000 100000");
  cgroup_.setMemory(1000, 300);
  cgroup_.setMemoryMax("4096");

  EXPECT_THAT(reader_.cpuUsageUsec(), IsOkAndHolds(5000U));
  EXPECT_THAT(reader_.cpuLimitCores(), IsOkAndHolds(1.5));
  EXPECT_THAT(reader_.memoryWorkingSetBytes(), IsOkAndHolds(700U));
  EXPECT_THAT(reader_.memoryLimitBytes(), IsOkAndHolds(4096U));
}

TEST_F(CgroupReaderTest, WorkingSetNeverNegative) {
  cgroup_.setMemory(100, 300);
  EXPECT_THAT(reader_.memoryWorkingSetBytes(), IsOkAndHolds(0U));
}

TEST_F(CgroupReaderTest, MissingFiles) {
  for (const std::string file :
       {"cpu.stat", "cpu.max", "memory.current", "memory.stat", "memory.max"}) {
    cgroup_.removeFile(file);
  }
  EXPECT_THAT(reader_.cpuUsageUsec(),
              HasStatus(absl::StatusCode::kUnavailable, HasSubstr("cpu.stat")));
  EXPECT_THAT(reader_.cpuLimitCores(),
              HasStatus(absl::StatusCode::kUnavailable, HasSubstr("cpu.max")));
  EXPECT_THAT(reader_.memoryWorkingSetBytes(),
              HasStatus(absl::StatusCode::kUnavailable, HasSubstr("memory.current")));
  EXPECT_THAT(reader_.memoryLimitBytes(),
              HasStatus(absl::StatusCode::kUnavailable, HasSubstr("memory.max")));

  // memory.stat is read after memory.current.
  cgroup_.setMemory(100, 0);
  cgroup_.removeFile("memory.stat");
  EXPECT_THAT(reader_.memoryWorkingSetBytes(),
              HasStatus(absl::StatusCode::kUnavailable, HasSubstr("memory.stat")));
}

TEST_F(CgroupReaderTest, InvalidContent) {
  cgroup_.setMemory(100, 0);
  TestEnvironment::writeStringToFileForTest(cgroup_.directory() + "/memory.current", "abc", true);
  EXPECT_THAT(reader_.memoryWorkingSetBytes(), HasStatusCode(absl::StatusCode::kInvalidArgument));

  cgroup_.setMemory(100, 0);
  TestEnvironment::writeStringToFileForTest(cgroup_.directory() + "/memory.stat", "anon 1\n", true);
  EXPECT_THAT(reader_.memoryWorkingSetBytes(), HasStatusCode(absl::StatusCode::kNotFound));
}

} // namespace
} // namespace OrcaReporter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
