#include <fstream>

#include "envoy/extensions/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "absl/strings/ascii.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

TEST(DynamicModuleTestGeneral, InvalidPath) {
  absl::StatusOr<DynamicModulePtr> result = newDynamicModule("invalid_name", false);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

INSTANTIATE_TEST_SUITE_P(LanguageTests, DynamicModuleTestLanguages, testing::Values("c", "rust"),
                         DynamicModuleTestLanguages::languageParamToTestName);

TEST_P(DynamicModuleTestLanguages, DoNotClose) {
  std::string language = GetParam();
  using GetSomeVariableFuncType = int (*)(void);
  absl::StatusOr<DynamicModulePtr> module =
      newDynamicModule(testSharedObjectPath("no_op", language), false);
  EXPECT_TRUE(module.ok());
  const auto getSomeVariable =
      module->get()->getFunctionPointer<GetSomeVariableFuncType>("getSomeVariable");
  EXPECT_TRUE(getSomeVariable.ok());
  EXPECT_EQ(getSomeVariable.value()(), 1);
  EXPECT_EQ(getSomeVariable.value()(), 2);
  EXPECT_EQ(getSomeVariable.value()(), 3);

  // Release the module, and reload it.
  module->reset();
  module = newDynamicModule(testSharedObjectPath("no_op", language), true);
  EXPECT_TRUE(module.ok());

  // This module must be reloaded and the variable must be reset.
  const auto getSomeVariable2 =
      (module->get()->getFunctionPointer<GetSomeVariableFuncType>("getSomeVariable"));
  EXPECT_TRUE(getSomeVariable2.ok());
  EXPECT_EQ(getSomeVariable2.value()(), 1); // Start from 1 again.
  EXPECT_EQ(getSomeVariable2.value()(), 2);
  EXPECT_EQ(getSomeVariable2.value()(), 3);

  // Release the module, and reload it.
  module->reset();
  module = newDynamicModule(testSharedObjectPath("no_op", language), false);
  EXPECT_TRUE(module.ok());

  // This module must be the already loaded one, and the variable must be kept.
  const auto getSomeVariable3 =
      module->get()->getFunctionPointer<GetSomeVariableFuncType>("getSomeVariable");
  EXPECT_TRUE(getSomeVariable3.ok());
  EXPECT_EQ(getSomeVariable3.value()(), 4); // Start from 4.
}

TEST(DynamicModuleTestLanguages, InitFunctionOnlyCalledOnce) {
  const auto path = testSharedObjectPath("program_init_assert", "c");
  absl::StatusOr<DynamicModulePtr> m1 = newDynamicModule(path, false);
  EXPECT_TRUE(m1.ok());
  // At this point, m1 is alive, so the init function should have been called.
  // When creating a new module with the same path, the init function should not be called again.
  absl::StatusOr<DynamicModulePtr> m2 = newDynamicModule(path, false);
  EXPECT_TRUE(m2.ok());
  m1->reset();
  m2->reset();

  // Even with the do_not_close=true, init function should only be called once.
  m1 = newDynamicModule(path, true);
  EXPECT_TRUE(m1.ok());
  m1->reset(); // Closing the module, but the module is still alive in the process.
  // This m2 should point to the same module as m1 whose handle is already freed, but
  // the init function should not be called again.
  m2 = newDynamicModule(path, true);
  EXPECT_TRUE(m2.ok());
}

TEST(DynamicModuleTestLanguages, LoadLibGlobally) {
  const auto path = testSharedObjectPath("program_global", "c");
  absl::StatusOr<DynamicModulePtr> module = newDynamicModule(path, false, true);
  EXPECT_TRUE(module.ok());

  // The child module should be able to access the symbol from the global module.
  const auto child_path = testSharedObjectPath("program_child", "c");
  absl::StatusOr<DynamicModulePtr> child_module = newDynamicModule(child_path, false, false);
  EXPECT_TRUE(child_module.ok());

  using GetSomeVariableFuncType = int (*)(void);
  const auto getSomeVariable =
      child_module->get()->getFunctionPointer<GetSomeVariableFuncType>("getSomeVariable");
  EXPECT_TRUE(getSomeVariable.ok());
  EXPECT_EQ(getSomeVariable.value()(), 42);
}

TEST_P(DynamicModuleTestLanguages, NoProgramInit) {
  std::string language = GetParam();
  absl::StatusOr<DynamicModulePtr> result =
      newDynamicModule(testSharedObjectPath("no_program_init", language), false);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Failed to resolve symbol envoy_dynamic_module_on_program_init"));
}

TEST_P(DynamicModuleTestLanguages, ProgramInitFail) {
  std::string language = GetParam();
  absl::StatusOr<DynamicModulePtr> result =
      newDynamicModule(testSharedObjectPath("program_init_fail", language), false);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Failed to initialize dynamic module:"));
}

TEST_P(DynamicModuleTestLanguages, ABIVersionMismatch) {
  // We expect a warning log for ABI version mismatch but still load the module successfully.
  std::string language = GetParam();
  absl::StatusOr<DynamicModulePtr> result =
      newDynamicModule(testSharedObjectPath("abi_version_mismatch", language), false);
  EXPECT_TRUE(result.ok());
}

TEST(CreateDynamicModulesByName, EnvoyDynamicModulesSearchPathSet) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute(
          "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
      1);

  absl::StatusOr<DynamicModulePtr> module = newDynamicModuleByName("no_op", false);
  EXPECT_TRUE(module.ok()) << "Failed to load module: " << module.status().message();
  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST(CreateDynamicModulesByName, EnvoyDynamicModulesSearchPathNotSetFallbackToCwd) {
  std::filesystem::path test_lib = testSharedObjectPath("no_op", "c");
  std::filesystem::path staged_lib = TestEnvironment::substitute("{{ test_rundir }}/libfoo.so");
  std::filesystem::copy(test_lib, staged_lib);
  absl::StatusOr<DynamicModulePtr> module = newDynamicModuleByName("foo", false);
  EXPECT_TRUE(module.ok()) << "Failed to load module: " << module.status().message();
  std::filesystem::remove(staged_lib);
}

TEST(CreateDynamicModulesByName, DlopenDefaultSearchPath) {
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", "/should/not/find/this/path", 1);

  std::filesystem::path test_lib = testSharedObjectPath("no_op", "c");
  std::filesystem::path staged_lib =
      TestEnvironment::substitute("{{ test_rundir }}/libwhatever.so");
  std::filesystem::copy(test_lib, staged_lib);
  absl::StatusOr<DynamicModulePtr> module = newDynamicModuleByName("whatever", false);
  EXPECT_TRUE(module.ok()) << "Failed to load module: " << module.status().message();

  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
  std::filesystem::remove(staged_lib);
}

TEST(StaticModule, LoadSuccess) {
  absl::StatusOr<DynamicModulePtr> result = newStaticModule("matcher_no_op_static");
  EXPECT_TRUE(result.ok()) << result.status().message();
}

TEST(StaticModule, SymbolNotFound) {
  // "nonexistent_module" has no prefixed symbols in the binary.
  absl::StatusOr<DynamicModulePtr> result = newStaticModule("nonexistent_module");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Failed to resolve symbol "
                                 "envoy_dynamic_module_on_program_init"));
}

TEST(StaticModule, MultipleLoads) {
  absl::StatusOr<DynamicModulePtr> c_module =
      newDynamicModuleByName("matcher_no_op_static", /*do_not_close=*/false);
  EXPECT_TRUE(c_module.ok()) << c_module.status().message();

  absl::StatusOr<DynamicModulePtr> c_module_2 =
      newDynamicModuleByName("matcher_no_op_static", /*do_not_close=*/false);
  EXPECT_TRUE(c_module_2.ok()) << c_module_2.status().message();
}

TEST(CreateDynamicModulesByName, ModuleNotFound) {
  absl::StatusOr<DynamicModulePtr> module = newDynamicModuleByName("no_op", false);
  EXPECT_FALSE(module.ok());
  EXPECT_EQ(module.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(module.status().message(),
              testing::HasSubstr(
                  "Failed to load dynamic module: libno_op.so not found in any search path"));
}

TEST(NewDynamicModuleFromBytes, Success) {
  std::filesystem::path test_lib = testSharedObjectPath("no_op", "c");
  std::ifstream input(test_lib, std::ios::binary);
  ASSERT_TRUE(input.good()) << "Failed to open test shared object file: " << test_lib;
  const std::string module_bytes((std::istreambuf_iterator<char>(input)),
                                 std::istreambuf_iterator<char>());

  const std::string sha256 = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
  // Ensure no leftover from previous runs.
  const std::filesystem::path temp_path =
      std::filesystem::temp_directory_path() / fmt::format("envoy_dynamic_module_{}.so", sha256);
  std::filesystem::remove(temp_path);

  absl::StatusOr<DynamicModulePtr> module =
      newDynamicModuleFromBytes(module_bytes, sha256, false, false);
  EXPECT_TRUE(module.ok()) << "Failed to load module from bytes: " << module.status().message();
  EXPECT_TRUE(std::filesystem::exists(temp_path));

  // Cleanup.
  module->reset();
  std::filesystem::remove(temp_path);
}

TEST(NewDynamicModuleFromBytes, InvalidBytes) {
  const std::string garbage = "this is not a valid shared object";
  const std::string sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
  const std::filesystem::path temp_path =
      std::filesystem::temp_directory_path() / fmt::format("envoy_dynamic_module_{}.so", sha256);
  std::filesystem::remove(temp_path);

  absl::StatusOr<DynamicModulePtr> module =
      newDynamicModuleFromBytes(garbage, sha256, false, false);
  EXPECT_FALSE(module.ok());
  EXPECT_EQ(module.status().code(), absl::StatusCode::kInvalidArgument);

  // The invalid file should have been cleaned up.
  EXPECT_FALSE(std::filesystem::exists(temp_path));
}

TEST(VerifyFileSha256, RoundTripWithComputedDigest) {
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "envoy_verify_sha256_round_trip.bin";
  const std::string contents = "the quick brown fox jumps over the lazy dog";
  {
    std::ofstream out(tmp, std::ios::binary);
    out << contents;
  }
  Buffer::OwnedImpl hash_buffer(contents);
  const std::string expected_hex =
      Hex::encode(::Envoy::Common::Crypto::UtilitySingleton::get().getSha256Digest(hash_buffer));
  EXPECT_TRUE(verifyFileSha256(tmp, expected_hex).ok());
  std::filesystem::remove(tmp);
}

TEST(VerifyFileSha256, MismatchReturnsFailedPrecondition) {
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "envoy_verify_sha256_mismatch.bin";
  {
    std::ofstream out(tmp, std::ios::binary);
    out << "envoy";
  }
  const std::string wrong_sha = "0000000000000000000000000000000000000000000000000000000000000000";
  auto status = verifyFileSha256(tmp, wrong_sha);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), testing::HasSubstr("SHA256 mismatch"));
  EXPECT_THAT(std::string(status.message()), testing::HasSubstr(wrong_sha));
  std::filesystem::remove(tmp);
}

TEST(VerifyFileSha256, MissingFileReturnsInternal) {
  const std::filesystem::path missing =
      std::filesystem::temp_directory_path() / "envoy_verify_sha256_missing.bin";
  std::filesystem::remove(missing);
  ASSERT_FALSE(std::filesystem::exists(missing));
  auto status =
      verifyFileSha256(missing, "0000000000000000000000000000000000000000000000000000000000000000");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInternal);
  EXPECT_THAT(std::string(status.message()),
              testing::HasSubstr("Failed to open file for SHA256 verification"));
}

TEST(VerifyFileSha256, EmptyFileMatchesEmptyDigest) {
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "envoy_verify_sha256_empty.bin";
  { std::ofstream out(tmp, std::ios::binary); }
  // SHA256 of the empty input.
  const std::string empty_sha = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  EXPECT_TRUE(verifyFileSha256(tmp, empty_sha).ok());
  std::filesystem::remove(tmp);
}

TEST(VerifyFileSha256, LargeFileExceedingChunkSize) {
  // Force the helper through multiple iterations of its 64 KiB read loop. 200 KiB picks up
  // three full chunks plus a partial tail, exercising the streaming `SHA256_Update` path.
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "envoy_verify_sha256_large.bin";
  std::string contents(200 * 1024, '\0');
  for (size_t i = 0; i < contents.size(); ++i) {
    contents[i] = static_cast<char>(i & 0xFF);
  }
  {
    std::ofstream out(tmp, std::ios::binary);
    out.write(contents.data(), contents.size());
  }
  Buffer::OwnedImpl hash_buffer(contents);
  const std::string expected_hex =
      Hex::encode(::Envoy::Common::Crypto::UtilitySingleton::get().getSha256Digest(hash_buffer));
  EXPECT_TRUE(verifyFileSha256(tmp, expected_hex).ok());
  std::filesystem::remove(tmp);
}

TEST(VerifyFileSha256, MixedCaseExpectedHexNormalised) {
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "envoy_verify_sha256_mixed_case.bin";
  const std::string contents = "case-insensitive-hash";
  {
    std::ofstream out(tmp, std::ios::binary);
    out << contents;
  }
  Buffer::OwnedImpl hash_buffer(contents);
  std::string expected_hex =
      Hex::encode(::Envoy::Common::Crypto::UtilitySingleton::get().getSha256Digest(hash_buffer));
  // Upper-case the expected hex and verify it still matches (the helper lower-cases before
  // comparing).
  for (char& c : expected_hex) {
    c = absl::ascii_toupper(c);
  }
  EXPECT_TRUE(verifyFileSha256(tmp, expected_hex).ok());
  std::filesystem::remove(tmp);
}

TEST(NewDynamicModuleFromBytes, RepeatedLoadReusesDlopenHandle) {
  std::filesystem::path test_lib = testSharedObjectPath("no_op", "c");
  std::ifstream input(test_lib, std::ios::binary);
  ASSERT_TRUE(input.good());
  const std::string module_bytes((std::istreambuf_iterator<char>(input)),
                                 std::istreambuf_iterator<char>());

  const std::string sha256 = "1111111111111111111111111111111111111111111111111111111111111111";
  const std::filesystem::path temp_path =
      std::filesystem::temp_directory_path() / fmt::format("envoy_dynamic_module_{}.so", sha256);
  std::filesystem::remove(temp_path);

  // First load writes and loads the module.
  absl::StatusOr<DynamicModulePtr> module1 =
      newDynamicModuleFromBytes(module_bytes, sha256, true, false);
  ASSERT_TRUE(module1.ok()) << module1.status().message();
  ASSERT_TRUE(std::filesystem::exists(temp_path));

  // Second load with the same sha256 — writes again but the RTLD_NOLOAD check in
  // newDynamicModule returns the existing handle, so the init function is not called twice.
  absl::StatusOr<DynamicModulePtr> module2 =
      newDynamicModuleFromBytes(module_bytes, sha256, true, false);
  ASSERT_TRUE(module2.ok()) << module2.status().message();

  // Cleanup.
  module1->reset();
  module2->reset();
  std::filesystem::remove(temp_path);
}

namespace {

// Returns the hex-encoded SHA256 of the given bytes.
std::string sha256Hex(const std::string& bytes) {
  Buffer::OwnedImpl buffer(bytes);
  return Hex::encode(::Envoy::Common::Crypto::UtilitySingleton::get().getSha256Digest(buffer));
}

// Reads the raw bytes of the test shared object at the given path.
std::string readModuleBytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  EXPECT_TRUE(input.good()) << "failed to open " << path;
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

// Unit tests for newDynamicModuleByConfig(), covering every sourcing branch: by-name, local file,
// the various invalid-config rejections, and all of the remote HTTP source paths (no context, cache
// hit, tampered cache, cache-load failure, NACK, missing init manager, missing callback, and the
// asynchronous fetch).
class NewDynamicModuleByConfigTest : public testing::Test {
protected:
  NewDynamicModuleByConfigTest() {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);
  }
  ~NewDynamicModuleByConfigTest() override {
    TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
  }

  // Builds a config whose module is sourced from a remote HTTP data source with the given SHA256.
  ProtoDynamicModuleConfig makeRemoteConfig(absl::string_view sha256) {
    ProtoDynamicModuleConfig config;
    auto* remote = config.mutable_module()->mutable_remote();
    remote->mutable_http_uri()->set_uri("https://example.com/module.so");
    remote->mutable_http_uri()->set_cluster("cluster_1");
    remote->mutable_http_uri()->mutable_timeout()->set_seconds(5);
    remote->set_sha256(std::string(sha256));
    return config;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

// Neither name nor module set: the config is rejected. No context is needed for this path.
TEST_F(NewDynamicModuleByConfigTest, NoModuleNorName) {
  ProtoDynamicModuleConfig config;
  auto result = newDynamicModuleByConfig(config, "test_module");
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Either 'name' or 'module' must be specified"));
}

// By-name loading succeeds synchronously and requires no context (the context-less caller path).
TEST_F(NewDynamicModuleByConfigTest, ByNameSuccess) {
  ProtoDynamicModuleConfig config;
  config.set_name("no_op");
  auto result = newDynamicModuleByConfig(config, "test_module");
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(result->loaded, nullptr);
  EXPECT_EQ(result->async, nullptr);
}

// By-name loading of a missing module reports a load error.
TEST_F(NewDynamicModuleByConfigTest, ByNameFailure) {
  ProtoDynamicModuleConfig config;
  config.set_name("nonexistent_module");
  auto result = newDynamicModuleByConfig(config, "test_module");
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Failed to load dynamic module"));
}

// Local-file loading succeeds synchronously and requires no context (the context-less caller path).
TEST_F(NewDynamicModuleByConfigTest, LocalFileSuccess) {
  ProtoDynamicModuleConfig config;
  config.mutable_module()->mutable_local()->set_filename(testSharedObjectPath("no_op", "c"));
  auto result = newDynamicModuleByConfig(config, "test_module");
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(result->loaded, nullptr);
  EXPECT_EQ(result->async, nullptr);
}

// Local-file loading of a missing path reports a load error.
TEST_F(NewDynamicModuleByConfigTest, LocalFileFailure) {
  ProtoDynamicModuleConfig config;
  config.mutable_module()->mutable_local()->set_filename("/nonexistent/path/to/module.so");
  auto result = newDynamicModuleByConfig(config, "test_module");
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Failed to load dynamic module"));
}

// A module data source that is neither a local file path nor a remote source is rejected.
TEST_F(NewDynamicModuleByConfigTest, ModuleWithoutLocalFileOrRemoteRejected) {
  ProtoDynamicModuleConfig config;
  config.mutable_module()->mutable_local()->set_inline_bytes("AAAA");
  auto result = newDynamicModuleByConfig(config, "test_module");
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Only local file path or remote HTTP source is supported"));
}

// A remote source without a factory context is rejected (the context-less caller cannot fetch).
TEST_F(NewDynamicModuleByConfigTest, RemoteWithoutContextRejected) {
  auto result = newDynamicModuleByConfig(makeRemoteConfig("abc123"), "test_module");
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Remote module sources require a factory context"));
}

// A cached file whose contents match the expected SHA256 is loaded directly (cache hit).
TEST_F(NewDynamicModuleByConfigTest, RemoteCacheHitLoads) {
  const std::string module_path = testSharedObjectPath("no_op", "c");
  const std::string sha = sha256Hex(readModuleBytes(module_path));
  const auto cached = moduleTempPath(sha);
  std::filesystem::create_directories(cached.parent_path());
  std::filesystem::copy_file(module_path, cached,
                             std::filesystem::copy_options::overwrite_existing);

  auto result = newDynamicModuleByConfig(makeRemoteConfig(sha), "test_module", context_);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(result->loaded, nullptr);
  EXPECT_EQ(result->async, nullptr);

  std::filesystem::remove(cached);
}

// A cached file whose contents do not match the expected SHA256 is removed and the code falls
// through to the fetch path (which here rejects because no init manager is supplied).
TEST_F(NewDynamicModuleByConfigTest, RemoteCacheHitTamperedRemoved) {
  const std::string expected_sha = sha256Hex(readModuleBytes(testSharedObjectPath("no_op", "c")));
  const auto cached = moduleTempPath(expected_sha);
  std::filesystem::create_directories(cached.parent_path());
  {
    std::ofstream out(cached, std::ios::binary);
    out << "tampered bytes that do not match the expected sha256";
  }
  ASSERT_TRUE(std::filesystem::exists(cached));

  auto result = newDynamicModuleByConfig(makeRemoteConfig(expected_sha), "test_module", context_);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Remote module sources require an init manager"));
  // The tampered cache entry must have been removed.
  EXPECT_FALSE(std::filesystem::exists(cached));
}

// A cached file whose contents match the expected SHA256 but is not a valid shared object reports
// a clear load error without re-fetching.
TEST_F(NewDynamicModuleByConfigTest, RemoteCacheHitValidShaButLoadFails) {
  const std::string garbage = "this is not a valid shared object";
  const std::string sha = sha256Hex(garbage);
  const auto cached = moduleTempPath(sha);
  std::filesystem::create_directories(cached.parent_path());
  {
    std::ofstream out(cached, std::ios::binary);
    out << garbage;
  }

  auto result = newDynamicModuleByConfig(makeRemoteConfig(sha), "test_module", context_);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Cached remote module failed to load"));

  std::filesystem::remove(cached);
}

// In NACK mode a cache miss is rejected and a background fetch is kicked off.
TEST_F(NewDynamicModuleByConfigTest, RemoteNackOnCacheMiss) {
  const std::string sha = "deadbeef1234567890abcdef1234567890abcdef1234567890abcdef12345678";
  std::filesystem::remove(moduleTempPath(sha));

  auto config = makeRemoteConfig(sha);
  config.set_nack_on_cache_miss(true);
  // The cluster is not initialized in the mock, so the background fetch fails fast; the config is
  // still rejected as a cache miss.
  auto result = newDynamicModuleByConfig(config, "test_module", context_);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("not cached"));
}

// A remote source with a context but no init manager is rejected.
TEST_F(NewDynamicModuleByConfigTest, RemoteNoCacheNoInitManager) {
  const std::string sha = "1111111111111111111111111111111111111111111111111111111111111111";
  std::filesystem::remove(moduleTempPath(sha));

  auto result = newDynamicModuleByConfig(makeRemoteConfig(sha), "test_module", context_);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Remote module sources require an init manager"));
}

// A remote source with a context and init manager but no on_loaded callback is rejected.
TEST_F(NewDynamicModuleByConfigTest, RemoteNoOnLoadedCallbackRejected) {
  const std::string sha = "2222222222222222222222222222222222222222222222222222222222222222";
  std::filesystem::remove(moduleTempPath(sha));

  NiceMock<Init::MockManager> init_manager;
  auto result =
      newDynamicModuleByConfig(makeRemoteConfig(sha), "test_module", context_, init_manager,
                               /*on_loaded=*/nullptr);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Remote module sources require an on_loaded callback"));
}

// A remote source with a context, init manager, and on_loaded callback starts an asynchronous
// fetch and returns the async loading state (the callback is only invoked once the fetch resolves).
TEST_F(NewDynamicModuleByConfigTest, RemoteAsyncReturnsAsyncState) {
  const std::string sha = "3333333333333333333333333333333333333333333333333333333333333333";
  std::filesystem::remove(moduleTempPath(sha));

  NiceMock<Init::MockManager> init_manager;
  bool on_loaded_called = false;
  auto result =
      newDynamicModuleByConfig(makeRemoteConfig(sha), "test_module", context_, init_manager,
                               [&on_loaded_called](DynamicModulePtr) { on_loaded_called = true; });
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result->loaded, nullptr);
  ASSERT_NE(result->async, nullptr);
  EXPECT_NE(result->async->remote_provider, nullptr);
  // The fetch has not been initialized, so the callback has not run yet.
  EXPECT_FALSE(on_loaded_called);
}

} // namespace

// Reads a dynamic_modules.<leaf> counter tagged config_name=<name>. Re-creates the same tagged
// counter (idempotent lookup), so it does not depend on how the store renders tags into a flat
// name.
uint64_t configLoadFailureValue(Stats::Scope& scope, absl::string_view leaf,
                                absl::string_view config_name) {
  Stats::StatNameDynamicPool pool(scope.symbolTable());
  Stats::StatNameTagVector tags{{pool.add("config_name"), pool.add(config_name)}};
  return Stats::Utility::counterFromElements(
             scope, {Stats::DynamicName(DynamicModulesStatRoot), Stats::DynamicName(leaf)}, tags)
      .value();
}

TEST(DynamicModuleStats, IncrementConfigLoadFailure) {
  Stats::IsolatedStoreImpl store;
  Stats::Scope& scope = *store.rootScope();
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ON_CALL(context, scope()).WillByDefault(testing::ReturnRef(scope));

  // Repeated failures with the same config_name accumulate on one series.
  incrementLoadFailure(context, "my-filter", ModuleLoadErrorStat);
  incrementLoadFailure(context, "my-filter", ModuleLoadErrorStat);
  EXPECT_EQ(2U, configLoadFailureValue(scope, ModuleLoadErrorStat, "my-filter"));

  // Distinct leaves and distinct config_names are independent series.
  EXPECT_EQ(0U, configLoadFailureValue(scope, RemoteFetchErrorStat, "my-filter"));
  EXPECT_EQ(0U, configLoadFailureValue(scope, ModuleLoadErrorStat, "other"));

  // An empty config_name falls back to "default".
  incrementLoadFailure(context, "", RemoteFetchErrorStat);
  EXPECT_EQ(1U, configLoadFailureValue(scope, RemoteFetchErrorStat, "default"));

  // An absent context is a no-op (the context-less caller path).
  incrementLoadFailure(absl::nullopt, "my-filter", ModuleLoadErrorStat);
  EXPECT_EQ(2U, configLoadFailureValue(scope, ModuleLoadErrorStat, "my-filter"));
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
