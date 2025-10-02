#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"
#include "envoy/singleton/manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/filesystem/directory.h"
#include "source/extensions/filters/http/cache_v2/cache_entry_utils.h"
#include "source/extensions/filters/http/cache_v2/cache_headers_utils.h"
#include "source/extensions/filters/http/cache_v2/cache_sessions.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/cache_eviction_thread.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_fixed_block.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/cache_file_header_proto_util.h"
#include "source/extensions/http/cache_v2/file_system_http_cache/file_system_http_cache.h"

#include "test/extensions/common/async_files/mocks.h"
#include "test/extensions/filters/http/cache_v2/http_cache_implementation_test_common.h"
#include "test/extensions/filters/http/cache_v2/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/cleanup/cleanup.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace FileSystemHttpCache {

using Common::AsyncFiles::AsyncFileHandle;
using Common::AsyncFiles::MockAsyncFileContext;
using Common::AsyncFiles::MockAsyncFileHandle;
using Common::AsyncFiles::MockAsyncFileManager;
using Common::AsyncFiles::MockAsyncFileManagerFactory;
using ::envoy::extensions::filters::http::cache_v2::v3::CacheV2Config;
using StatusHelpers::HasStatusCode;
using StatusHelpers::IsOkAndHolds;
using ::testing::HasSubstr;
using ::testing::IsNull;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

MATCHER(PopulatedLookup, "") { return arg.populated(); }

absl::string_view yaml_config = R"(
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.http.cache_v2.file_system_http_cache.v3.FileSystemHttpCacheV2Config
    manager_config:
      thread_pool:
        thread_count: 1
    cache_path: /tmp
)";

class FileSystemCacheTestContext {
public:
  FileSystemCacheTestContext() {
    cache_path_ = absl::StrCat(env_.temporaryDirectory(), "/");
    ConfigProto cfg = testConfig();
    deleteCacheFiles(cfg.cache_path());
    auto cache_config = cacheConfig(cfg);
    const std::string type{
        TypeUtil::typeUrlToDescriptorFullName(cache_config.typed_config().type_url())};
    http_cache_factory_ = Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(type);
    if (http_cache_factory_ == nullptr) {
      throw EnvoyException(
          fmt::format("Didn't find a registered implementation for type: '{}'", type));
    }
    ON_CALL(context_.server_factory_context_.api_, threadFactory())
        .WillByDefault([]() -> Thread::ThreadFactory& { return Thread::threadFactoryForTest(); });
  }

  void initCache() { cache_ = *http_cache_factory_->getCache(cacheConfig(testConfig()), context_); }

  void waitForEvictionThreadIdle() { cache()->cache_eviction_thread_.waitForIdle(); }

  ConfigProto testConfig() {
    envoy::extensions::filters::http::cache_v2::v3::CacheV2Config cache_config;
    TestUtility::loadFromYaml(std::string(yaml_config), cache_config);
    ConfigProto cfg;
    EXPECT_TRUE(MessageUtil::unpackTo(cache_config.typed_config(), cfg).ok());
    cfg.set_cache_path(cache_path_);
    return cfg;
  }

  envoy::extensions::filters::http::cache_v2::v3::CacheV2Config cacheConfig(ConfigProto cfg) {
    envoy::extensions::filters::http::cache_v2::v3::CacheV2Config cache_config;
    cache_config.mutable_typed_config()->PackFrom(cfg);
    return cache_config;
  }

protected:
  void deleteCacheFiles(std::string path) {
    for (const auto& it : ::Envoy::Filesystem::Directory(path)) {
      if (absl::StartsWith(it.name_, "cache-")) {
        env_.removePath(absl::StrCat(path, it.name_));
      }
    }
  }

  FileSystemHttpCache* cache() { return dynamic_cast<FileSystemHttpCache*>(&cache_->cache()); }
  ::Envoy::TestEnvironment env_;
  std::string cache_path_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::shared_ptr<CacheSessions> cache_;
  HttpCacheFactory* http_cache_factory_;
};

class FileSystemHttpCacheTestWithNoDefaultCache : public FileSystemCacheTestContext,
                                                  public ::testing::Test {};

TEST_F(FileSystemHttpCacheTestWithNoDefaultCache, InitialStatsAreSetCorrectly) {
  const std::string file_1_contents = "XXXXX";
  const std::string file_2_contents = "YYYYYYYYYY";
  const uint64_t max_count = 99;
  const uint64_t max_size = 87654321;
  ConfigProto cfg = testConfig();
  cfg.mutable_max_cache_entry_count()->set_value(max_count);
  cfg.mutable_max_cache_size_bytes()->set_value(max_size);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-a"), file_1_contents, true);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-b"), file_2_contents, true);
  cache_ = *http_cache_factory_->getCache(cacheConfig(cfg), context_);
  waitForEvictionThreadIdle();
  EXPECT_EQ(cache()->stats().size_limit_bytes_.value(), max_size);
  EXPECT_EQ(cache()->stats().size_limit_count_.value(), max_count);
  EXPECT_EQ(cache()->stats().size_bytes_.value(), file_1_contents.size() + file_2_contents.size());
  EXPECT_EQ(cache()->stats().size_count_.value(), 2);
  EXPECT_EQ(cache()->stats().eviction_runs_.value(), 0);
}

TEST_F(FileSystemHttpCacheTestWithNoDefaultCache, EvictsOldestFilesUntilUnderCountLimit) {
  const std::string file_contents = "XXXXX";
  const uint64_t max_count = 2;
  ConfigProto cfg = testConfig();
  cfg.mutable_max_cache_entry_count()->set_value(max_count);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-a"), file_contents, true);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-b"), file_contents, true);
  // TODO(#24994): replace this with backdating the files when that's possible.
  sleep(1); // NO_CHECK_FORMAT(real_time)
  cache_ = *http_cache_factory_->getCache(cacheConfig(cfg), context_);
  waitForEvictionThreadIdle();
  EXPECT_EQ(cache()->stats().eviction_runs_.value(), 0);
  EXPECT_EQ(cache()->stats().size_bytes_.value(), file_contents.size() * 2);
  EXPECT_EQ(cache()->stats().size_count_.value(), 2);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-c"), file_contents, true);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-d"), file_contents, true);
  cache()->trackFileAdded(file_contents.size());
  cache()->trackFileAdded(file_contents.size());
  waitForEvictionThreadIdle();
  EXPECT_EQ(cache()->stats().size_bytes_.value(), file_contents.size() * 2);
  EXPECT_EQ(cache()->stats().size_count_.value(), 2);
  EXPECT_FALSE(Filesystem::fileSystemForTest().fileExists(absl::StrCat(cache_path_, "cache-a")));
  EXPECT_FALSE(Filesystem::fileSystemForTest().fileExists(absl::StrCat(cache_path_, "cache-b")));
  EXPECT_TRUE(Filesystem::fileSystemForTest().fileExists(absl::StrCat(cache_path_, "cache-c")));
  EXPECT_TRUE(Filesystem::fileSystemForTest().fileExists(absl::StrCat(cache_path_, "cache-d")));
  // There may have been one or two eviction runs here, because there's a race
  // between the eviction and the second file being added. Either amount of runs
  // is valid, as the eventual consistency is achieved either way.
  EXPECT_THAT(cache()->stats().eviction_runs_.value(), testing::AnyOf(1, 2));
}

TEST_F(FileSystemHttpCacheTestWithNoDefaultCache, EvictsOldestFilesUntilUnderSizeLimit) {
  const std::string file_contents = "XXXXX";
  const std::string large_file_contents = "XXXXXXXXXXXX";
  const uint64_t max_size = large_file_contents.size();
  ConfigProto cfg = testConfig();
  cfg.mutable_max_cache_size_bytes()->set_value(max_size);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-a"), file_contents, true);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-b"), file_contents, true);
  // TODO(#24994): replace this with backdating the files when that's possible.
  sleep(1); // NO_CHECK_FORMAT(real_time)
  cache_ = *http_cache_factory_->getCache(cacheConfig(cfg), context_);
  waitForEvictionThreadIdle();
  EXPECT_EQ(cache()->stats().eviction_runs_.value(), 0);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-c"), large_file_contents, true);
  EXPECT_EQ(cache()->stats().size_bytes_.value(), file_contents.size() * 2);
  EXPECT_EQ(cache()->stats().size_count_.value(), 2);
  cache()->trackFileAdded(large_file_contents.size());
  waitForEvictionThreadIdle();
  EXPECT_EQ(cache()->stats().size_bytes_.value(), large_file_contents.size());
  EXPECT_EQ(cache()->stats().size_count_.value(), 1);
  EXPECT_FALSE(Filesystem::fileSystemForTest().fileExists(absl::StrCat(cache_path_, "cache-a")));
  EXPECT_FALSE(Filesystem::fileSystemForTest().fileExists(absl::StrCat(cache_path_, "cache-b")));
  EXPECT_TRUE(Filesystem::fileSystemForTest().fileExists(absl::StrCat(cache_path_, "cache-c")));
  EXPECT_EQ(cache()->stats().eviction_runs_.value(), 1);
}

class FileSystemHttpCacheTest : public FileSystemCacheTestContext, public ::testing::Test {
  void SetUp() override { initCache(); }
};

MATCHER_P2(IsStatTag, name, value, "") {
  if (!ExplainMatchResult(name, arg.name_, result_listener) ||
      !ExplainMatchResult(value, arg.value_, result_listener)) {
    *result_listener << "\nexpected {name: \"" << name << "\", value: \"" << value
                     << "\"},\n but got {name: \"" << arg.name_ << "\", value: \"" << arg.value_
                     << "\"}\n";
    return false;
  }
  return true;
}

TEST_F(FileSystemHttpCacheTest, StatsAreConstructedCorrectly) {
  std::string cache_path_no_periods = absl::StrReplaceAll(cache_path_, {{".", "_"}});
  // Validate that a gauge has appropriate name and tags.
  EXPECT_EQ(cache()->stats().size_bytes_.tagExtractedName(), "cache.size_bytes");
  EXPECT_THAT(cache()->stats().size_bytes_.tags(),
              ::testing::ElementsAre(IsStatTag("cache_path", cache_path_no_periods)));
  // Validate that a counter has appropriate name and tags.
  EXPECT_EQ(cache()->stats().eviction_runs_.tagExtractedName(), "cache.eviction_runs");
  EXPECT_THAT(cache()->stats().eviction_runs_.tags(),
              ::testing::ElementsAre(IsStatTag("cache_path", cache_path_no_periods)));
}

TEST_F(FileSystemHttpCacheTest, TrackFileRemovedClampsAtZero) {
  cache()->trackFileAdded(1);
  EXPECT_EQ(cache()->stats().size_bytes_.value(), 1);
  EXPECT_EQ(cache()->stats().size_count_.value(), 1);
  cache()->trackFileRemoved(8);
  EXPECT_EQ(cache()->stats().size_bytes_.value(), 0);
  EXPECT_EQ(cache()->stats().size_count_.value(), 0);
  // Remove a second time to ensure that count going below zero also clamps at zero.
  cache()->trackFileRemoved(8);
  EXPECT_EQ(cache()->stats().size_bytes_.value(), 0);
  EXPECT_EQ(cache()->stats().size_count_.value(), 0);
}

TEST_F(FileSystemHttpCacheTest,
       InvalidArgumentOnTryingToCreateCachesWithDistinctConfigsOnSamePath) {
  ConfigProto cfg = testConfig();
  cfg.mutable_manager_config()->mutable_thread_pool()->set_thread_count(2);
  EXPECT_THAT(http_cache_factory_->getCache(cacheConfig(cfg), context_),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
}

TEST_F(FileSystemHttpCacheTest, IdenticalCacheConfigReturnsSameCacheInstance) {
  ConfigProto cfg = testConfig();
  auto second_cache = http_cache_factory_->getCache(cacheConfig(cfg), context_);
  EXPECT_EQ(cache_, *second_cache);
}

TEST_F(FileSystemHttpCacheTest, CacheConfigsWithDifferentPathsReturnDistinctCacheInstances) {
  ConfigProto cfg = testConfig();
  cfg.set_cache_path("/tmp");
  auto second_cache = http_cache_factory_->getCache(cacheConfig(cfg), context_);
  EXPECT_NE(cache_, *second_cache);
}

class MockSingletonManager : public Singleton::ManagerImpl {
public:
  MockSingletonManager() {
    // By default just act like a real SingletonManager, but allow overrides.
    ON_CALL(*this, get)
        .WillByDefault(std::bind(&MockSingletonManager::realGet, this, std::placeholders::_1,
                                 std::placeholders::_2, std::placeholders::_3));
  }

  MOCK_METHOD(Singleton::InstanceSharedPtr, get,
              (const std::string& name, Singleton::SingletonFactoryCb cb, bool pin));
  Singleton::InstanceSharedPtr realGet(const std::string& name, Singleton::SingletonFactoryCb cb,
                                       bool pin) {
    return Singleton::ManagerImpl::get(name, cb, pin);
  }
};

class FileSystemHttpCacheTestWithMockFiles : public FileSystemHttpCacheTest {
public:
  FileSystemHttpCacheTestWithMockFiles() {
    ON_CALL(context_.server_factory_context_, singletonManager())
        .WillByDefault(ReturnRef(mock_singleton_manager_));
    ON_CALL(mock_singleton_manager_, get(HasSubstr("async_file_manager_factory_singleton"), _, _))
        .WillByDefault(Return(mock_async_file_manager_factory_));
    ON_CALL(*mock_async_file_manager_factory_, getAsyncFileManager(_, _))
        .WillByDefault(Return(mock_async_file_manager_));
    request_headers_.setMethod("GET");
    request_headers_.setHost("example.com");
    request_headers_.setScheme("https");
    request_headers_.setCopy(Http::CustomHeaders::get().CacheControl, "max-age=3600");
    request_headers_.setPath("/");
    expect_false_callback_ = [this](bool result) {
      EXPECT_FALSE(result);
      false_callbacks_called_++;
    };
    expect_true_callback_ = [this](bool result) {
      EXPECT_TRUE(result);
      true_callbacks_called_++;
    };
    key_ = CacheHeadersUtils::makeKey(request_headers_, "fake-cluster");
    headers_size_ = headerProtoSize(makeCacheFileHeaderProto(key_, response_headers_, metadata_));
  }

  void setTrailers(Http::TestResponseTrailerMapImpl trailers) {
    response_trailers_ = trailers;
    trailers_size_ = bufferFromProto(makeCacheFileTrailerProto(response_trailers_)).length();
  }

  void setBodySize(size_t sz) { body_size_ = sz; }

  CacheFileFixedBlock testHeaderBlock() {
    CacheFileFixedBlock block;
    block.setHeadersSize(headers_size_);
    block.setTrailersSize(trailers_size_);
    block.setBodySize(body_size_);
    return block;
  }

  Buffer::InstancePtr testHeaderBlockBuffer() {
    auto buffer = std::make_unique<Buffer::OwnedImpl>();
    testHeaderBlock().serializeToBuffer(*buffer);
    return buffer;
  }

  CacheFileHeader testHeaderProto() {
    return makeCacheFileHeaderProto(key_, response_headers_, metadata_);
  }

  Buffer::InstancePtr testHeaderBuffer() {
    return std::make_unique<Buffer::OwnedImpl>(bufferFromProto(testHeaderProto()));
  }

  Buffer::InstancePtr undersizedBuffer() { return std::make_unique<Buffer::OwnedImpl>("x"); }

  CacheFileTrailer testTrailerProto() { return makeCacheFileTrailerProto(response_trailers_); }

  Buffer::InstancePtr testTrailerBuffer() {
    return std::make_unique<Buffer::OwnedImpl>(bufferFromProto(testTrailerProto()));
  }

  void SetUp() override { initCache(); }

  void testLookup(absl::StatusOr<LookupResult>* lookup_result_out) {
    cache()->lookup(LookupRequest{Key{key_}, *dispatcher_},
                    [lookup_result_out](absl::StatusOr<LookupResult>&& result) {
                      *lookup_result_out = std::move(result);
                    });
    pumpDispatcher();
  }

  void testSuccessfulLookup(absl::StatusOr<LookupResult>* lookup_result_out) {
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
    EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
    if (trailers_size_) {
      EXPECT_CALL(*mock_async_file_handle_,
                  read(_, testHeaderBlock().offsetToTrailers(), trailers_size_, _));
    }
    EXPECT_CALL(*mock_async_file_handle_,
                read(_, testHeaderBlock().offsetToHeaders(), headers_size_, _));
    testLookup(lookup_result_out);
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<Buffer::InstancePtr>(testHeaderBlockBuffer()));
    pumpDispatcher();
    if (trailers_size_) {
      mock_async_file_manager_->nextActionCompletes(
          absl::StatusOr<Buffer::InstancePtr>(testTrailerBuffer()));
      pumpDispatcher();
    }
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<Buffer::InstancePtr>(testHeaderBuffer()));
    pumpDispatcher();
    // result should be populated.
    ASSERT_THAT(*lookup_result_out, IsOkAndHolds(PopulatedLookup()));
  }

  void pumpDispatcher() { dispatcher_->run(Event::Dispatcher::RunType::Block); }

protected:
  ::testing::NiceMock<MockSingletonManager> mock_singleton_manager_;
  std::shared_ptr<MockCacheProgressReceiver> cache_progress_receiver_ =
      std::make_shared<MockCacheProgressReceiver>();
  std::shared_ptr<MockAsyncFileManagerFactory> mock_async_file_manager_factory_ =
      std::make_shared<NiceMock<MockAsyncFileManagerFactory>>();
  std::shared_ptr<MockAsyncFileManager> mock_async_file_manager_ =
      std::make_shared<NiceMock<MockAsyncFileManager>>();
  MockAsyncFileHandle mock_async_file_handle_ =
      std::make_shared<StrictMock<MockAsyncFileContext>>(mock_async_file_manager_);
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Event::SimulatedTimeSystem time_system_;
  Http::TestRequestHeaderMapImpl request_headers_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  Http::TestResponseHeaderMapImpl response_headers_{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"},
  };
  Http::TestResponseTrailerMapImpl response_trailers_{{"fruit", "banana"}};
  const ResponseMetadata metadata_{time_system_.systemTime()};
  Key key_;
  int false_callbacks_called_ = 0;
  int true_callbacks_called_ = 0;
  std::function<void(bool result)> expect_false_callback_;
  std::function<void(bool result)> expect_true_callback_;
  size_t headers_size_;
  size_t trailers_size_{0};
  size_t body_size_{0};
  Api::ApiPtr api_ = Api::createApiForTest();
  Event::DispatcherPtr dispatcher_ = api_->allocateDispatcher("test_thread");
};

TEST_F(FileSystemHttpCacheTestWithMockFiles, NotFoundForReadReturnsMiss) {
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
  absl::StatusOr<LookupResult> lookup_result;
  testLookup(&lookup_result);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(absl::NotFoundError("forced not-found")));
  pumpDispatcher();
  EXPECT_FALSE(lookup_result.value().populated());
  // File handle didn't get used but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close(nullptr, [](absl::Status) {}));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, FailedReadOfHeaderBlockReturnsError) {
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
  EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
  absl::StatusOr<LookupResult> lookup_result;
  testLookup(&lookup_result);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(absl::UnknownError("intentional failure to read")));
  pumpDispatcher();
  EXPECT_THAT(lookup_result, HasStatusCode(absl::StatusCode::kUnknown));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, SuccessfulEvictDecreasesStats) {
  // Fake-add two files of size 12345, so we can validate the stats decrease of removing a file.
  cache()->trackFileAdded(12345);
  cache()->trackFileAdded(12345);
  EXPECT_EQ(cache()->stats().size_bytes_.value(), 2 * 12345);
  EXPECT_EQ(cache()->stats().size_count_.value(), 2);
  EXPECT_CALL(*mock_async_file_manager_, stat);
  EXPECT_CALL(*mock_async_file_manager_, unlink);
  cache()->evict(*dispatcher_, key_);
  pumpDispatcher();
  struct stat stat_result = {};
  stat_result.st_size = 12345;
  // stat
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  pumpDispatcher();
  // unlink
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
  pumpDispatcher();
  // Should have deducted the size of the file that got deleted. Since we started at 2 * 12345,
  // this should make the value 12345.
  EXPECT_EQ(cache()->stats().size_bytes_.value(), 12345);
  // Should have deducted one file for the file that got deleted. Since we started at 2,
  // this should make the value 1.
  EXPECT_EQ(cache()->stats().size_count_.value(), 1);
  // File handle didn't get used but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close(nullptr, [](absl::Status) {}));
}

Buffer::InstancePtr invalidHeaderBlock() {
  CacheFileFixedBlock block;
  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  block.serializeToBuffer(*buffer);
  // Replace the four byte id at the start with a bad id.
  buffer->drain(4);
  buffer->prepend("BAD!");
  return buffer;
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, ReadWithInvalidHeaderBlockReturnsError) {
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
  EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
  absl::StatusOr<LookupResult> lookup_result;
  testLookup(&lookup_result);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(invalidHeaderBlock()));
  pumpDispatcher();
  EXPECT_THAT(lookup_result, HasStatusCode(absl::StatusCode::kDataLoss));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, ReadWithIncompleteHeaderBlockReturnsError) {
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
  EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
  absl::StatusOr<LookupResult> lookup_result;
  testLookup(&lookup_result);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(undersizedBuffer()));
  pumpDispatcher();
  EXPECT_THAT(lookup_result, HasStatusCode(absl::StatusCode::kDataLoss));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, FailedReadOfHeaderProtoReturnsError) {
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
  EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_, read(_, CacheFileFixedBlock::size(), headers_size_, _));
  absl::StatusOr<LookupResult> lookup_result;
  testLookup(&lookup_result);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlockBuffer()));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(absl::UnknownError("intentional failure to read")));
  pumpDispatcher();
  EXPECT_THAT(lookup_result, HasStatusCode(absl::StatusCode::kUnknown));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, IncompleteReadOfHeaderProtoReturnsError) {
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
  EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_, read(_, CacheFileFixedBlock::size(), headers_size_, _));
  absl::StatusOr<LookupResult> lookup_result;
  testLookup(&lookup_result);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlockBuffer()));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(undersizedBuffer()));
  pumpDispatcher();
  EXPECT_THAT(lookup_result, HasStatusCode(absl::StatusCode::kDataLoss));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, FailedReadOfBodyProvokesReset) {
  setBodySize(10);
  absl::StatusOr<LookupResult> lookup_result;
  testSuccessfulLookup(&lookup_result);
  EXPECT_CALL(*mock_async_file_handle_, read(_, testHeaderBlock().offsetToBody(), 8, _));
  Buffer::InstancePtr got_body;
  EndStream got_end_stream = EndStream::More;
  lookup_result.value().cache_reader_->getBody(*dispatcher_, AdjustedByteRange(0, 8),
                                               [&](Buffer::InstancePtr body, EndStream end_stream) {
                                                 got_body = std::move(body);
                                                 got_end_stream = end_stream;
                                               });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(absl::UnknownError("intentional failure to read")));
  pumpDispatcher();
  EXPECT_THAT(got_body, IsNull());
  EXPECT_EQ(got_end_stream, EndStream::Reset);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, IncompleteReadOfBodyProvokesReset) {
  setBodySize(10);
  absl::StatusOr<LookupResult> lookup_result;
  testSuccessfulLookup(&lookup_result);
  EXPECT_CALL(*mock_async_file_handle_, read(_, testHeaderBlock().offsetToBody(), 8, _));
  Buffer::InstancePtr got_body;
  EndStream got_end_stream = EndStream::More;
  lookup_result.value().cache_reader_->getBody(*dispatcher_, AdjustedByteRange(0, 8),
                                               [&](Buffer::InstancePtr body, EndStream end_stream) {
                                                 got_body = std::move(body);
                                                 got_end_stream = end_stream;
                                               });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(undersizedBuffer()));
  pumpDispatcher();
  EXPECT_THAT(got_body, IsNull());
  EXPECT_EQ(got_end_stream, EndStream::Reset);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, FailedReadOfTrailersReturnsError) {
  setTrailers({{"fruit", "banana"}});
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
  EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_,
              read(_, testHeaderBlock().offsetToTrailers(), trailers_size_, _));
  absl::StatusOr<LookupResult> lookup_result;
  testLookup(&lookup_result);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlockBuffer()));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(absl::UnknownError("intentional failure to read")));
  pumpDispatcher();
  // result should be populated.
  EXPECT_THAT(lookup_result, HasStatusCode(absl::StatusCode::kUnknown));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, IncompleteReadOfTrailersReturnsError) {
  setTrailers({{"fruit", "banana"}});
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
  EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_,
              read(_, testHeaderBlock().offsetToTrailers(), trailers_size_, _));
  absl::StatusOr<LookupResult> lookup_result;
  testLookup(&lookup_result);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlockBuffer()));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(undersizedBuffer()));
  pumpDispatcher();
  // result should be populated.
  EXPECT_THAT(lookup_result, HasStatusCode(absl::StatusCode::kDataLoss));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToCreateFile) {
  EXPECT_CALL(*cache_progress_receiver_, onInsertFailed);
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile);
  cache()->insert(*dispatcher_, key_,
                  Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_), metadata_,
                  nullptr, cache_progress_receiver_);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(absl::UnknownError("intentionally failed to create file")));
  pumpDispatcher();
  // File handle didn't get used but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close(nullptr, [](absl::Status) {}));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersFailingToReadHeaderBlockAborts) {
  EXPECT_LOG_CONTAINS("error", "failed to read header block", {
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
    EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
    cache()->updateHeaders(*dispatcher_, key_, response_headers_, metadata_);
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<Buffer::InstancePtr>(absl::UnknownError("intentionally failed")));
    pumpDispatcher();
  });
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersIncompleteReadHeaderBlockAborts) {
  EXPECT_LOG_CONTAINS("error", "incomplete read of header block", {
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
    EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
    cache()->updateHeaders(*dispatcher_, key_, response_headers_, metadata_);
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<Buffer::InstancePtr>(undersizedBuffer()));
    pumpDispatcher();
  });
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersFailureToTruncateAborts) {
  EXPECT_LOG_CONTAINS("error", "failed to truncate headers", {
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
    EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
    EXPECT_CALL(*mock_async_file_handle_, truncate(_, testHeaderBlock().offsetToHeaders(), _));
    cache()->updateHeaders(*dispatcher_, key_, response_headers_, metadata_);
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<Buffer::InstancePtr>(testHeaderBlockBuffer()));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(absl::UnknownError("intentionally failed"));
    pumpDispatcher();
  });
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersFailureToOverwriteHeaderBlockAborts) {
  EXPECT_LOG_CONTAINS("error", "overwriting headers failed", {
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
    EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
    EXPECT_CALL(*mock_async_file_handle_, truncate(_, testHeaderBlock().offsetToHeaders(), _));
    EXPECT_CALL(*mock_async_file_handle_, write(_, _, 0, _));
    cache()->updateHeaders(*dispatcher_, key_, response_headers_, metadata_);
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<Buffer::InstancePtr>(testHeaderBlockBuffer()));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<size_t>(absl::UnknownError("intentionally failed")));
    pumpDispatcher();
  });
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersIncompleteOverwriteHeaderBlockAborts) {
  EXPECT_LOG_CONTAINS("error", "overwriting headers failed", {
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
    EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
    EXPECT_CALL(*mock_async_file_handle_, truncate(_, testHeaderBlock().offsetToHeaders(), _));
    EXPECT_CALL(*mock_async_file_handle_, write(_, _, 0, _));
    cache()->updateHeaders(*dispatcher_, key_, response_headers_, metadata_);
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<Buffer::InstancePtr>(testHeaderBlockBuffer()));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(1));
    pumpDispatcher();
  });
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersFailureToWriteHeadersAborts) {
  EXPECT_LOG_CONTAINS("error", "failed to write new headers", {
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
    EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
    EXPECT_CALL(*mock_async_file_handle_, truncate(_, testHeaderBlock().offsetToHeaders(), _));
    EXPECT_CALL(*mock_async_file_handle_, write(_, _, 0, _));
    EXPECT_CALL(*mock_async_file_handle_, write(_, _, testHeaderBlock().offsetToHeaders(), _));
    cache()->updateHeaders(*dispatcher_, key_, response_headers_, metadata_);
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<Buffer::InstancePtr>(testHeaderBlockBuffer()));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<size_t>(absl::UnknownError("intentionally failed")));
    pumpDispatcher();
  });
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersIncompleteWriteHeadersAborts) {
  EXPECT_LOG_CONTAINS("error", "incomplete write of new headers", {
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile);
    EXPECT_CALL(*mock_async_file_handle_, read(_, 0, CacheFileFixedBlock::size(), _));
    EXPECT_CALL(*mock_async_file_handle_, truncate(_, testHeaderBlock().offsetToHeaders(), _));
    EXPECT_CALL(*mock_async_file_handle_, write(_, _, 0, _));
    EXPECT_CALL(*mock_async_file_handle_, write(_, _, testHeaderBlock().offsetToHeaders(), _));
    cache()->updateHeaders(*dispatcher_, key_, response_headers_, metadata_);
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<Buffer::InstancePtr>(testHeaderBlockBuffer()));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(
        absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
    pumpDispatcher();
    mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(1));
    pumpDispatcher();
  });
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, EvictWithStatFailureSilentlyAborts) {
  EXPECT_CALL(*mock_async_file_manager_, stat);
  cache()->evict(*dispatcher_, key_);
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<struct stat>(absl::UnknownError("intentional failure")));
  pumpDispatcher();
  // File handle didn't get used but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close(nullptr, [](absl::Status) {}));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, EvictWithUnlinkFailureSilentlyAborts) {
  EXPECT_CALL(*mock_async_file_manager_, stat);
  EXPECT_CALL(*mock_async_file_manager_, unlink);
  cache()->evict(*dispatcher_, key_);
  pumpDispatcher();
  struct stat stat_result = {};
  stat_result.st_size = 12345;
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>(stat_result));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(absl::UnknownError("intentional failure"));
  pumpDispatcher();
  // File handle didn't get used but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close(nullptr, [](absl::Status) {}));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToDupFileHandle) {
  EXPECT_CALL(*cache_progress_receiver_, onInsertFailed);
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile);
  EXPECT_CALL(*mock_async_file_handle_, duplicate);
  cache()->insert(*dispatcher_, key_,
                  Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_), metadata_,
                  nullptr, cache_progress_receiver_);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(absl::UnknownError("intentionally failed to dup file")));
  pumpDispatcher();
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToWriteEmptyHeaderBlock) {
  auto duplicated_file_handle = std::make_shared<MockAsyncFileContext>();
  EXPECT_CALL(*duplicated_file_handle, close).WillOnce([]() { return []() {}; });
  auto http_source = std::make_unique<MockHttpSource>();
  EXPECT_CALL(*cache_progress_receiver_, onHeadersInserted);
  EXPECT_CALL(*cache_progress_receiver_, onInsertFailed);
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile);
  EXPECT_CALL(*mock_async_file_handle_, duplicate);
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, 0, _));
  cache()->insert(*dispatcher_, key_,
                  Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_), metadata_,
                  std::move(http_source), cache_progress_receiver_);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(duplicated_file_handle));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(
      absl::UnknownError("intentionally failed write to empty header block")));
  pumpDispatcher();
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToWriteBodyChunk) {
  auto duplicated_file_handle = std::make_shared<MockAsyncFileContext>();
  EXPECT_CALL(*duplicated_file_handle, close).WillOnce([]() { return []() {}; });
  auto http_source =
      std::make_unique<FakeStreamHttpSource>(*dispatcher_, nullptr, "abcde", nullptr);
  EXPECT_CALL(*cache_progress_receiver_, onHeadersInserted);
  EXPECT_CALL(*cache_progress_receiver_, onInsertFailed);
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile);
  EXPECT_CALL(*mock_async_file_handle_, duplicate);
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, 0, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, testHeaderBlock().offsetToBody(), _));
  cache()->insert(*dispatcher_, key_,
                  Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_), metadata_,
                  std::move(http_source), cache_progress_receiver_);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(duplicated_file_handle));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(testHeaderBlock().size()));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(absl::UnknownError("intentional fail to write body")));
  pumpDispatcher();
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertSilentlyAbortsOnFailureToWriteTrailerChunk) {
  setTrailers({{"fruit", "banana"}});
  auto duplicated_file_handle = std::make_shared<MockAsyncFileContext>();
  EXPECT_CALL(*duplicated_file_handle, close).WillOnce([]() { return []() {}; });
  auto http_source = std::make_unique<FakeStreamHttpSource>(
      *dispatcher_, nullptr, "",
      Http::createHeaderMap<Http::ResponseTrailerMapImpl>(response_trailers_));
  EXPECT_CALL(*cache_progress_receiver_, onHeadersInserted);
  EXPECT_CALL(*cache_progress_receiver_, onTrailersInserted);
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile);
  EXPECT_CALL(*mock_async_file_handle_, duplicate);
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, 0, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, testHeaderBlock().offsetToTrailers(), _));
  cache()->insert(*dispatcher_, key_,
                  Http::createHeaderMap<Http::ResponseHeaderMapImpl>(response_headers_), metadata_,
                  std::move(http_source), cache_progress_receiver_);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(duplicated_file_handle));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(testHeaderBlock().size()));
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(absl::UnknownError("intentional fail to write body")));
  pumpDispatcher();
}

// For the standard cache tests from http_cache_implementation_test_common.cc
// These will be run with the real file system, and therefore only cover the
// "no file errors" paths.
class FileSystemHttpCacheTestDelegate : public HttpCacheTestDelegate,
                                        public FileSystemCacheTestContext {
public:
  FileSystemHttpCacheTestDelegate() { initCache(); }
  HttpCache& cache() override { return cache_->cache(); }
  void beforePumpingDispatcher() override {
    dynamic_cast<FileSystemHttpCache&>(cache()).drainAsyncFileActionsForTest();
  }
};

// For the standard cache tests from http_cache_implementation_test_common.cc
INSTANTIATE_TEST_SUITE_P(FileSystemHttpCacheTest, HttpCacheImplementationTest,
                         testing::Values(std::make_unique<FileSystemHttpCacheTestDelegate>),
                         [](const testing::TestParamInfo<HttpCacheImplementationTest::ParamType>&) {
                           return "FileSystemHttpCache";
                         });

TEST(Registration, GetCacheFromFactory) {
  HttpCacheFactory* factory = Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(
      "envoy.extensions.http.cache_v2.file_system_http_cache.v3.FileSystemHttpCacheV2Config");
  ASSERT_NE(factory, nullptr);
  envoy::extensions::filters::http::cache_v2::v3::CacheV2Config cache_config;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  ON_CALL(factory_context.server_factory_context_.api_, threadFactory())
      .WillByDefault([]() -> Thread::ThreadFactory& { return Thread::threadFactoryForTest(); });
  TestUtility::loadFromYaml(std::string(yaml_config), cache_config);
  auto status_or_cache = factory->getCache(cache_config, factory_context);
  ASSERT_OK(status_or_cache);
  EXPECT_EQ((*status_or_cache)->cacheInfo().name_,
            "envoy.extensions.http.cache_v2.file_system_http_cache");
  // Verify that the config path got a / suffixed onto it.
  EXPECT_EQ(dynamic_cast<FileSystemHttpCache&>((*status_or_cache)->cache()).config().cache_path(),
            "/tmp/");
}

} // namespace FileSystemHttpCache
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
