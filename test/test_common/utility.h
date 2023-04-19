#pragma once

#include <cstdlib>
#include <list>
#include <random>
#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/buffer/buffer.h"
#include "envoy/network/address.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/store.h"
#include "envoy/thread/thread.h"
#include "envoy/type/matcher/v3/string.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/c_smart_ptr.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/thread.h"
#include "source/common/config/decoded_resource_impl.h"
#include "source/common/config/opaque_resource_decoder_impl.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/symbol_table.h"

#include "test/test_common/common_utility.h"
#include "test/test_common/file_system_for_test.h"
#include "test/test_common/logging.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_random_generator.h"
#include "test/test_common/test_time_system.h"
#include "test/test_common/thread_factory_for_test.h"

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_; // NOLINT(misc-unused-using-decls)
using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;
using testing::Invoke; //  NOLINT(misc-unused-using-decls)

namespace Envoy {

#if defined(__has_feature) && __has_feature(thread_sanitizer)
#define TSAN_TIMEOUT_FACTOR 3
#elif defined(ENVOY_CONFIG_COVERAGE)
#define TSAN_TIMEOUT_FACTOR 3
#else
#define TSAN_TIMEOUT_FACTOR 1
#endif

/*
  Macro to use for validating that a statement throws the specified type of exception, and that
  the exception's what() method returns a string which is matched by the specified matcher.
  This allows for expectations such as:

  EXPECT_THAT_THROWS_MESSAGE(
      bad_function_call(),
      EnvoyException,
      AllOf(StartsWith("expected prefix"), HasSubstr("some substring")));
*/
#define EXPECT_THAT_THROWS_MESSAGE(statement, expected_exception, matcher)                         \
  try {                                                                                            \
    statement;                                                                                     \
    ADD_FAILURE() << "Exception should take place. It did not.";                                   \
  } catch (expected_exception & e) {                                                               \
    EXPECT_THAT(std::string(e.what()), matcher);                                                   \
  }

// Expect that the statement throws the specified type of exception with exactly the specified
// message.
#define EXPECT_THROW_WITH_MESSAGE(statement, expected_exception, message)                          \
  EXPECT_THAT_THROWS_MESSAGE(statement, expected_exception, ::testing::Eq(message))

// Expect that the statement throws the specified type of exception with a message containing a
// substring matching the specified regular expression (i.e. the regex doesn't have to match
// the entire message).
#define EXPECT_THROW_WITH_REGEX(statement, expected_exception, regex_str)                          \
  EXPECT_THAT_THROWS_MESSAGE(statement, expected_exception, ::testing::ContainsRegex(regex_str))

// Expect that the statement throws the specified type of exception with a message that does not
// contain any substring matching the specified regular expression.
#define EXPECT_THROW_WITHOUT_REGEX(statement, expected_exception, regex_str)                       \
  EXPECT_THAT_THROWS_MESSAGE(statement, expected_exception,                                        \
                             ::testing::Not(::testing::ContainsRegex(regex_str)))

// Expect that the statement hits an ENVOY_BUG containing the specified message.
#if defined(NDEBUG) || defined(ENVOY_CONFIG_COVERAGE)
// ENVOY_BUGs in release mode or in a coverage test log error.
#define EXPECT_ENVOY_BUG(statement, message) EXPECT_LOG_CONTAINS("error", message, statement)
#else
// ENVOY_BUGs in (non-coverage) debug mode is fatal.
#define EXPECT_ENVOY_BUG(statement, message)                                                       \
  EXPECT_DEBUG_DEATH(statement, ::testing::HasSubstr(message))
#endif

#define VERBOSE_EXPECT_NO_THROW(statement)                                                         \
  try {                                                                                            \
    statement;                                                                                     \
  } catch (EnvoyException & e) {                                                                   \
    ADD_FAILURE() << "Unexpected exception: " << std::string(e.what());                            \
  }

#define VERIFY_ASSERTION(statement)                                                                \
  do {                                                                                             \
    ::testing::AssertionResult status = statement;                                                 \
    if (!status) {                                                                                 \
      return status;                                                                               \
    }                                                                                              \
  } while (false)

// A convenience macro for testing Envoy deprecated features. This will disable the test when
// tests are built with --define deprecated_features=disabled to avoid the hard-failure mode for
// deprecated features. Sample usage is:
//
// TEST_F(FixtureName, DEPRECATED_FEATURE_TEST(TestName)) {
// ...
// }
#ifndef ENVOY_DISABLE_DEPRECATED_FEATURES
#define DEPRECATED_FEATURE_TEST(X) X
#else
#define DEPRECATED_FEATURE_TEST(X) DISABLED_##X
#endif

/**
 * Wraps the common case of having a cross-thread "one shot" ready condition.
 *
 * It functions like absl::Notification except the usage of notifyAll() appears
 * to trigger tighter simultaneous wakeups in multiple threads, resulting in
 * more contentions, e.g. for BM_CreateRace in
 * ../common/stats/symbol_table_speed_test.cc.
 *
 * See
 *     https://github.com/abseil/abseil-cpp/blob/master/absl/synchronization/notification.h
 * for the absl impl, which appears to result in fewer contentions (and in
 * tests we want contentions).
 */
class ConditionalInitializer {
public:
  /**
   * Set the conditional to ready.
   */
  void setReady();

  /**
   * Block until the conditional is ready, will return immediately if it is already ready. This
   * routine will also reset ready_ so that the initializer can be used again. setReady() should
   * only be called once in between a call to waitReady().
   */
  void waitReady();

  /**
   * Waits until ready; does not reset it. This variation is immune to spurious
   * condvar wakeups, and is also suitable for having multiple threads wait on
   * a common condition.
   */
  void wait();

private:
  absl::Mutex mutex_;
  bool ready_ ABSL_GUARDED_BY(mutex_){false};
};

namespace Tracing {

class TestTraceContextImpl : public Tracing::TraceContext {
public:
  TestTraceContextImpl(const std::initializer_list<std::pair<std::string, std::string>>& values) {
    for (const auto& value : values) {
      context_map_[value.first] = value.second;
    }
  }
  absl::string_view protocol() const override { return context_protocol_; }
  absl::string_view host() const override { return context_host_; }
  absl::string_view path() const override { return context_path_; }
  absl::string_view method() const override { return context_method_; }
  void forEach(IterateCallback callback) const override {
    for (const auto& pair : context_map_) {
      if (!callback(pair.first, pair.second)) {
        break;
      }
    }
  }
  absl::optional<absl::string_view> getByKey(absl::string_view key) const override {
    auto iter = context_map_.find(key);
    if (iter == context_map_.end()) {
      return absl::nullopt;
    }
    return iter->second;
  }
  void setByKey(absl::string_view key, absl::string_view val) override {
    context_map_.insert({std::string(key), std::string(val)});
  }
  void setByReferenceKey(absl::string_view key, absl::string_view val) override {
    setByKey(key, val);
  }
  void setByReference(absl::string_view key, absl::string_view val) override { setByKey(key, val); }

  std::string context_protocol_;
  std::string context_host_;
  std::string context_path_;
  std::string context_method_;
  absl::flat_hash_map<std::string, std::string> context_map_;
};

} // namespace Tracing

namespace Http {

/**
 * All of the inline header functions that just pass through to the child header map.
 */
#define DEFINE_TEST_INLINE_HEADER_FUNCS(name)                                                      \
  const HeaderEntry* name() const override { return header_map_->name(); }                         \
  size_t remove##name() override {                                                                 \
    const size_t headers_removed = header_map_->remove##name();                                    \
    header_map_->verifyByteSizeInternalForTest();                                                  \
    return headers_removed;                                                                        \
  }                                                                                                \
  absl::string_view get##name##Value() const override { return header_map_->get##name##Value(); }  \
  void set##name(absl::string_view value) override {                                               \
    header_map_->set##name(value);                                                                 \
    header_map_->verifyByteSizeInternalForTest();                                                  \
  }

#define DEFINE_TEST_INLINE_STRING_HEADER_FUNCS(name)                                               \
public:                                                                                            \
  DEFINE_TEST_INLINE_HEADER_FUNCS(name)                                                            \
  void append##name(absl::string_view data, absl::string_view delimiter) override {                \
    header_map_->append##name(data, delimiter);                                                    \
    header_map_->verifyByteSizeInternalForTest();                                                  \
  }                                                                                                \
  void setReference##name(absl::string_view value) override {                                      \
    header_map_->setReference##name(value);                                                        \
    header_map_->verifyByteSizeInternalForTest();                                                  \
  }

#define DEFINE_TEST_INLINE_NUMERIC_HEADER_FUNCS(name)                                              \
public:                                                                                            \
  DEFINE_TEST_INLINE_HEADER_FUNCS(name)                                                            \
  void set##name(uint64_t value) override {                                                        \
    header_map_->set##name(value);                                                                 \
    header_map_->verifyByteSizeInternalForTest();                                                  \
  }

/**
 * Base class for all test header map types. This class wraps an underlying real header map
 * implementation, passes through all calls, and adds some niceties for testing that we don't
 * want in the production implementation for performance reasons. The wrapping functionality is
 * primarily here to deal with complexities around virtual calls in some constructor paths in
 * HeaderMapImpl.
 */
template <class Interface, class Impl> class TestHeaderMapImplBase : public Interface {
public:
  TestHeaderMapImplBase() = default;
  TestHeaderMapImplBase(const std::initializer_list<std::pair<std::string, std::string>>& values) {
    for (auto& value : values) {
      header_map_->addCopy(LowerCaseString(value.first), value.second);
    }
    header_map_->verifyByteSizeInternalForTest();
  }
  TestHeaderMapImplBase(const TestHeaderMapImplBase& rhs)
      : TestHeaderMapImplBase(*rhs.header_map_) {}
  TestHeaderMapImplBase(const HeaderMap& rhs) {
    HeaderMapImpl::copyFrom(*header_map_, rhs);
    header_map_->verifyByteSizeInternalForTest();
  }
  void copyFrom(const TestHeaderMapImplBase& rhs) { copyFrom(*rhs.header_map_); }
  void copyFrom(const HeaderMap& rhs) {
    HeaderMapImpl::copyFrom(*header_map_, rhs);
    header_map_->verifyByteSizeInternalForTest();
  }
  TestHeaderMapImplBase& operator=(const TestHeaderMapImplBase& rhs) {
    if (this == &rhs) {
      return *this;
    }
    clear();
    HeaderMapImpl::copyFrom(*header_map_, rhs);
    header_map_->verifyByteSizeInternalForTest();
    return *this;
  }

  // Value added methods on top of HeaderMap.
  void addCopy(const std::string& key, const std::string& value) {
    addCopy(LowerCaseString(key), value);
  }
  std::string get_(const std::string& key) const { return get_(LowerCaseString(key)); }
  std::string get_(const LowerCaseString& key) const {
    // TODO(mattklein123): Possibly allow getting additional headers beyond the first.
    auto headers = get(key);
    if (headers.empty()) {
      return EMPTY_STRING;
    } else {
      return std::string(headers[0]->value().getStringView());
    }
  }
  bool has(const std::string& key) const { return !get(LowerCaseString(key)).empty(); }
  bool has(const LowerCaseString& key) const { return !get(key).empty(); }
  size_t remove(const std::string& key) { return remove(LowerCaseString(key)); }

  // HeaderMap
  bool operator==(const HeaderMap& rhs) const override { return header_map_->operator==(rhs); }
  bool operator!=(const HeaderMap& rhs) const override { return header_map_->operator!=(rhs); }
  void addViaMove(HeaderString&& key, HeaderString&& value) override {
    header_map_->addViaMove(std::move(key), std::move(value));
    header_map_->verifyByteSizeInternalForTest();
  }
  void addReference(const LowerCaseString& key, absl::string_view value) override {
    header_map_->addReference(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void addReferenceKey(const LowerCaseString& key, uint64_t value) override {
    header_map_->addReferenceKey(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void addReferenceKey(const LowerCaseString& key, absl::string_view value) override {
    header_map_->addReferenceKey(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void addCopy(const LowerCaseString& key, uint64_t value) override {
    header_map_->addCopy(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void addCopy(const LowerCaseString& key, absl::string_view value) override {
    header_map_->addCopy(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void appendCopy(const LowerCaseString& key, absl::string_view value) override {
    header_map_->appendCopy(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void setReference(const LowerCaseString& key, absl::string_view value) override {
    header_map_->setReference(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void setReferenceKey(const LowerCaseString& key, absl::string_view value) override {
    header_map_->setReferenceKey(key, value);
  }
  void setCopy(const LowerCaseString& key, absl::string_view value) override {
    header_map_->setCopy(key, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  uint64_t byteSize() const override { return header_map_->byteSize(); }
  HeaderMap::GetResult get(const LowerCaseString& key) const override {
    return header_map_->get(key);
  }
  void iterate(HeaderMap::ConstIterateCb cb) const override { header_map_->iterate(cb); }
  void iterateReverse(HeaderMap::ConstIterateCb cb) const override {
    header_map_->iterateReverse(cb);
  }
  void clear() override {
    header_map_->clear();
    header_map_->verifyByteSizeInternalForTest();
  }
  size_t remove(const LowerCaseString& key) override {
    size_t headers_removed = header_map_->remove(key);
    header_map_->verifyByteSizeInternalForTest();
    return headers_removed;
  }
  size_t removeIf(const HeaderMap::HeaderMatchPredicate& predicate) override {
    size_t headers_removed = header_map_->removeIf(predicate);
    header_map_->verifyByteSizeInternalForTest();
    return headers_removed;
  }
  size_t removePrefix(const LowerCaseString& key) override {
    size_t headers_removed = header_map_->removePrefix(key);
    header_map_->verifyByteSizeInternalForTest();
    return headers_removed;
  }
  size_t size() const override { return header_map_->size(); }
  bool empty() const override { return header_map_->empty(); }
  void dumpState(std::ostream& os, int indent_level = 0) const override {
    header_map_->dumpState(os, indent_level);
  }

  using Handle = typename CustomInlineHeaderRegistry::Handle<Interface::header_map_type>;
  const HeaderEntry* getInline(Handle handle) const override {
    return header_map_->getInline(handle);
  }
  void appendInline(Handle handle, absl::string_view data, absl::string_view delimiter) override {
    header_map_->appendInline(handle, data, delimiter);
    header_map_->verifyByteSizeInternalForTest();
  }
  void setReferenceInline(Handle handle, absl::string_view value) override {
    header_map_->setReferenceInline(handle, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void setInline(Handle handle, absl::string_view value) override {
    header_map_->setInline(handle, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  void setInline(Handle handle, uint64_t value) override {
    header_map_->setInline(handle, value);
    header_map_->verifyByteSizeInternalForTest();
  }
  size_t removeInline(Handle handle) override {
    const size_t rc = header_map_->removeInline(handle);
    header_map_->verifyByteSizeInternalForTest();
    return rc;
  }
  StatefulHeaderKeyFormatterOptConstRef formatter() const override {
    return StatefulHeaderKeyFormatterOptConstRef(header_map_->formatter());
  }
  StatefulHeaderKeyFormatterOptRef formatter() override { return header_map_->formatter(); }

  std::unique_ptr<Impl> header_map_{Impl::create()};
};

/**
 * Typed test implementations for all of the concrete header types.
 */
class TestRequestHeaderMapImpl
    : public TestHeaderMapImplBase<RequestHeaderMap, RequestHeaderMapImpl> {
public:
  using TestHeaderMapImplBase::TestHeaderMapImplBase;

  INLINE_REQ_STRING_HEADERS(DEFINE_TEST_INLINE_STRING_HEADER_FUNCS)
  INLINE_REQ_NUMERIC_HEADERS(DEFINE_TEST_INLINE_NUMERIC_HEADER_FUNCS)
  INLINE_REQ_RESP_STRING_HEADERS(DEFINE_TEST_INLINE_STRING_HEADER_FUNCS)
  INLINE_REQ_RESP_NUMERIC_HEADERS(DEFINE_TEST_INLINE_NUMERIC_HEADER_FUNCS)

  // Tracing::TraceContext
  absl::string_view protocol() const override { return header_map_->getProtocolValue(); }
  absl::string_view host() const override { return header_map_->getHostValue(); }
  absl::string_view path() const override { return header_map_->getPathValue(); }
  absl::string_view method() const override { return header_map_->getMethodValue(); }
  void forEach(IterateCallback callback) const override {
    ASSERT(header_map_);
    header_map_->iterate([cb = std::move(callback)](const HeaderEntry& entry) {
      if (cb(entry.key().getStringView(), entry.value().getStringView())) {
        return HeaderMap::Iterate::Continue;
      }
      return HeaderMap::Iterate::Break;
    });
  }
  absl::optional<absl::string_view> getByKey(absl::string_view key) const override {
    ASSERT(header_map_);
    return header_map_->getByKey(key);
  }
  void setByKey(absl::string_view key, absl::string_view value) override {
    ASSERT(header_map_);
    header_map_->setByKey(key, value);
  }
  void setByReference(absl::string_view key, absl::string_view val) override {
    ASSERT(header_map_);
    header_map_->setByReference(key, val);
  }
  void setByReferenceKey(absl::string_view key, absl::string_view val) override {
    ASSERT(header_map_);
    header_map_->setByReferenceKey(key, val);
  }
};

using TestRequestTrailerMapImpl = TestHeaderMapImplBase<RequestTrailerMap, RequestTrailerMapImpl>;

class TestResponseHeaderMapImpl
    : public TestHeaderMapImplBase<ResponseHeaderMap, ResponseHeaderMapImpl> {
public:
  using TestHeaderMapImplBase::TestHeaderMapImplBase;

  INLINE_RESP_STRING_HEADERS(DEFINE_TEST_INLINE_STRING_HEADER_FUNCS)
  INLINE_RESP_NUMERIC_HEADERS(DEFINE_TEST_INLINE_NUMERIC_HEADER_FUNCS)
  INLINE_REQ_RESP_STRING_HEADERS(DEFINE_TEST_INLINE_STRING_HEADER_FUNCS)
  INLINE_REQ_RESP_NUMERIC_HEADERS(DEFINE_TEST_INLINE_NUMERIC_HEADER_FUNCS)
  INLINE_RESP_STRING_HEADERS_TRAILERS(DEFINE_TEST_INLINE_STRING_HEADER_FUNCS)
  INLINE_RESP_NUMERIC_HEADERS_TRAILERS(DEFINE_TEST_INLINE_NUMERIC_HEADER_FUNCS)
};

class TestResponseTrailerMapImpl
    : public TestHeaderMapImplBase<ResponseTrailerMap, ResponseTrailerMapImpl> {
public:
  using TestHeaderMapImplBase::TestHeaderMapImplBase;

  INLINE_RESP_STRING_HEADERS_TRAILERS(DEFINE_TEST_INLINE_STRING_HEADER_FUNCS)
  INLINE_RESP_NUMERIC_HEADERS_TRAILERS(DEFINE_TEST_INLINE_NUMERIC_HEADER_FUNCS)
};

// Helper method to create a header map from an initializer list. Useful due to make_unique's
// inability to infer the initializer list type.
template <class T>
inline std::unique_ptr<T>
makeHeaderMap(const std::initializer_list<std::pair<std::string, std::string>>& values) {
  return std::make_unique<T, const std::initializer_list<std::pair<std::string, std::string>>&>(
      values);
}

} // namespace Http

namespace Api {
ApiPtr createApiForTest();
ApiPtr createApiForTest(Filesystem::Instance& filesystem);
ApiPtr createApiForTest(Random::RandomGenerator& random);
ApiPtr createApiForTest(Stats::Store& stat_store);
ApiPtr createApiForTest(Stats::Store& stat_store, Random::RandomGenerator& random);
ApiPtr createApiForTest(Event::TimeSystem& time_system);
ApiPtr createApiForTest(Stats::Store& stat_store, Event::TimeSystem& time_system);
} // namespace Api

// Useful for testing ScopeTrackedObject order of deletion.
class MessageTrackedObject : public ScopeTrackedObject {
public:
  MessageTrackedObject(absl::string_view sv) : sv_(sv) {}
  void dumpState(std::ostream& os, int /*indent_level*/) const override { os << sv_; }

private:
  absl::string_view sv_;
};

MATCHER_P(HeaderMapEqualIgnoreOrder, expected, "") {
  const bool equal = TestUtility::headerMapEqualIgnoreOrder(*arg, *expected);
  if (!equal) {
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("Expected header map:") << "\n"
                     << *expected
                     << TestUtility::addLeftAndRightPadding("is not equal to actual header map:")
                     << "\n"
                     << *arg << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

MATCHER_P(ProtoEq, expected, "") {
  const bool equal =
      TestUtility::protoEqual(arg, expected, /*ignore_repeated_field_ordering=*/false);
  if (!equal) {
    *result_listener << "\n"
                     << "==========================Expected proto:===========================\n"
                     << expected.DebugString()
                     << "------------------is not equal to actual proto:---------------------\n"
                     << arg.DebugString()
                     << "====================================================================\n";
  }
  return equal;
}

MATCHER_P(ProtoEqIgnoreRepeatedFieldOrdering, expected, "") {
  const bool equal =
      TestUtility::protoEqual(arg, expected, /*ignore_repeated_field_ordering=*/true);
  if (!equal) {
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("Expected proto:") << "\n"
                     << expected.DebugString()
                     << TestUtility::addLeftAndRightPadding("is not equal to actual proto:") << "\n"
                     << arg.DebugString()
                     << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

MATCHER_P2(ProtoEqIgnoringField, expected, ignored_field, "") {
  const bool equal = TestUtility::protoEqualIgnoringField(arg, expected, ignored_field);
  if (!equal) {
    std::string but_ignoring = absl::StrCat("(but ignoring ", ignored_field, ")");
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("Expected proto:") << "\n"
                     << TestUtility::addLeftAndRightPadding(but_ignoring) << "\n"
                     << expected.DebugString()
                     << TestUtility::addLeftAndRightPadding("is not equal to actual proto:") << "\n"
                     << arg.DebugString()
                     << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

MATCHER_P(RepeatedProtoEq, expected, "") {
  const bool equal = TestUtility::repeatedPtrFieldEqual(arg, expected);
  if (!equal) {
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("Expected repeated:") << "\n"
                     << RepeatedPtrUtil::debugString(expected) << "\n"
                     << TestUtility::addLeftAndRightPadding("is not equal to actual repeated:")
                     << "\n"
                     << RepeatedPtrUtil::debugString(arg) << "\n"
                     << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

MATCHER_P(DecodedResourcesEq, expected, "") {
  const bool equal = std::equal(arg.begin(), arg.end(), expected.begin(), expected.end(),
                                TestUtility::decodedResourceEq);
  if (!equal) {
    const auto format_resources =
        [](const std::vector<Config::DecodedResourceRef>& resources) -> std::string {
      std::vector<std::string> resource_strs;
      std::transform(
          resources.begin(), resources.end(), std::back_inserter(resource_strs),
          [](const Config::DecodedResourceRef& resource) -> std::string {
            return fmt::format(
                "<name: {}, aliases: {}, version: {}, resource: {}>", resource.get().name(),
                absl::StrJoin(resource.get().aliases(), ","), resource.get().version(),
                resource.get().hasResource() ? resource.get().resource().DebugString() : "(none)");
          });
      return absl::StrJoin(resource_strs, ", ");
    };
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("Expected resources:") << "\n"
                     << format_resources(expected) << "\n"
                     << TestUtility::addLeftAndRightPadding("are not equal to actual resources:")
                     << "\n"
                     << format_resources(arg) << "\n"
                     << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

MATCHER_P(Percent, rhs, "") {
  envoy::type::v3::FractionalPercent expected;
  expected.set_numerator(rhs);
  expected.set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  return TestUtility::protoEqual(expected, arg, /*ignore_repeated_field_ordering=*/false);
}

MATCHER_P(JsonStringEq, expected, "") {
  const bool equal = TestUtility::jsonStringEqual(arg, expected);
  if (!equal) {
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("Expected JSON string:") << "\n"
                     << expected
                     << TestUtility::addLeftAndRightPadding("is not equal to actual JSON string:")
                     << "\n"
                     << arg << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

} // namespace Envoy
