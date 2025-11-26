#include "source/common/http/header_map_impl.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/common/tracing/trace_context_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Tracing {
namespace {

TEST(TraceContextHandlerTest, TraceContextHandlerGetTest) {
  // The key will be lowercase.
  {
    TraceContextHandler handler("KEY");
    EXPECT_EQ("key", handler.key().get());
  }

  TraceContextHandler normal_key("key");
  TraceContextHandler inline_key("x-forwarded-for"); // This key is inline key for HTTP.

  TraceContextHandler unknown_normal_key("unknown_normal_key");
  TraceContextHandler unknown_inline_key("x-envoy-original-path");

  // Test get.
  {
    auto headers = Http::RequestHeaderMapImpl::create();
    headers->addCopy(Http::LowerCaseString("key"), "value");
    headers->addCopy(Http::LowerCaseString("x-forwarded-for"), "127.0.0.1");

    HttpTraceContext http_tracer_context(*headers);
    TestTraceContextImpl trace_context{{"key", "value"}, {"x-forwarded-for", "127.0.0.1"}};

    EXPECT_EQ("value", normal_key.get(trace_context).value());
    EXPECT_EQ("127.0.0.1", inline_key.get(trace_context).value());

    EXPECT_EQ("value", normal_key.get(http_tracer_context).value());
    EXPECT_EQ("127.0.0.1", inline_key.get(http_tracer_context).value());
  }

  // Test get all.
  {

    Http::TestRequestHeaderMapImpl headers{{"key", "value1"},
                                           {"key", "value2"},
                                           {"x-forwarded-for", "127.0.0.1"},
                                           {"x-forwarded-for", "127.0.0.2"},
                                           {"other", "other_value"}};
    HttpTraceContext http_tracer_context(headers);
    TestTraceContextImpl trace_context{{"key", "value"}, {"x-forwarded-for", "127.0.0.1"}};

    EXPECT_EQ(normal_key.getAll(trace_context)[0], "value");
    EXPECT_EQ(inline_key.getAll(trace_context)[0], "127.0.0.1");

    auto multiple_values_of_normal_key = normal_key.getAll(http_tracer_context);
    EXPECT_EQ(multiple_values_of_normal_key.size(), 2);
    EXPECT_EQ(multiple_values_of_normal_key[0], "value1");
    EXPECT_EQ(multiple_values_of_normal_key[1], "value2");

    auto multiple_values_of_inline_key = inline_key.getAll(http_tracer_context);
    EXPECT_EQ(multiple_values_of_inline_key.size(), 1);
    EXPECT_EQ(multiple_values_of_inline_key[0], "127.0.0.1,127.0.0.2");

    EXPECT_TRUE(unknown_normal_key.getAll(http_tracer_context).empty());
    EXPECT_TRUE(unknown_inline_key.getAll(http_tracer_context).empty());
    EXPECT_TRUE(!unknown_normal_key.get(trace_context).has_value());
    EXPECT_TRUE(!unknown_inline_key.get(trace_context).has_value());
  }
}

TEST(TraceContextHandlerTest, TraceContextHandlerSetTest) {

  TraceContextHandler normal_key("key");
  TraceContextHandler inline_key("content-type"); // This key is inline key for HTTP.

  // Test set.
  {
    auto headers = Http::RequestHeaderMapImpl::create();
    HttpTraceContext http_tracer_context(*headers);

    TestTraceContextImpl trace_context{};

    normal_key.set(trace_context, "value");
    EXPECT_EQ("value", normal_key.get(trace_context).value());

    inline_key.set(trace_context, "text/html");
    EXPECT_EQ("text/html", inline_key.get(trace_context).value());

    normal_key.set(http_tracer_context, "value");
    EXPECT_EQ("value", normal_key.get(http_tracer_context).value());

    inline_key.set(http_tracer_context, "text/html");
    EXPECT_EQ("text/html", inline_key.get(http_tracer_context).value());
  }
}

TEST(TraceContextHandlerTest, TraceContextHandlerRemoveTest) {

  TraceContextHandler normal_key("key");
  TraceContextHandler inline_key("content-type"); // This key is inline key for HTTP.

  // Test remove.
  {
    auto headers = Http::RequestHeaderMapImpl::create();
    headers->setContentType("text/plain");
    headers->addCopy(Http::LowerCaseString("key"), "value");

    HttpTraceContext http_tracer_context(*headers);
    TestTraceContextImpl trace_context{{"key", "value"}, {"content-type", "text/plain"}};

    normal_key.remove(trace_context);
    EXPECT_FALSE(normal_key.get(trace_context).has_value());

    inline_key.remove(trace_context);
    EXPECT_FALSE(inline_key.get(trace_context).has_value());

    normal_key.remove(http_tracer_context);
    EXPECT_FALSE(normal_key.get(http_tracer_context).has_value());

    inline_key.remove(http_tracer_context);
    EXPECT_FALSE(inline_key.get(http_tracer_context).has_value());
  }
}

TEST(TraceContextHandlerTest, TraceContextHandlerSetRefKeyTest) {

  TraceContextHandler normal_key("key");
  TraceContextHandler inline_key("content-type"); // This key is inline key for HTTP.
  // Test setRefKey.
  {
    auto headers = Http::RequestHeaderMapImpl::create();
    HttpTraceContext http_tracer_context(*headers);
    TestTraceContextImpl trace_context{};

    normal_key.setRefKey(trace_context, "value");
    auto iter = trace_context.context_map_.find("key");
    // setRefKey make no sense for non-HTTP context.
    EXPECT_NE(iter->first.data(), normal_key.key().get().data());

    inline_key.setRefKey(trace_context, "text/html");
    auto iter2 = trace_context.context_map_.find("content-type");
    // setRefKey make no sense for non-HTTP context.
    EXPECT_NE(iter2->first.data(), inline_key.key().get().data());

    normal_key.setRefKey(http_tracer_context, "value");
    auto iter3 = headers->get(Http::LowerCaseString("key"));
    // setRefKey make sense for HTTP context.
    EXPECT_EQ(iter3[0]->key().getStringView().data(), normal_key.key().get().data());

    inline_key.setRefKey(http_tracer_context, "text/html");
    auto iter4 = headers->get(Http::LowerCaseString("content-type"));
    // Note, setRefKey make no sense for inline key of HTTP context because
    // inline key is stored in central registry and won't be referenced.
    EXPECT_NE(iter4[0]->key().getStringView().data(), inline_key.key().get().data());
  }
}

TEST(TraceContextHandlerTest, TraceContextHandlerSetRefTest) {

  TraceContextHandler normal_key("key");
  TraceContextHandler inline_key("content-type"); // This key is inline key for HTTP.
  // Test setRef
  {
    auto headers = Http::RequestHeaderMapImpl::create();

    HttpTraceContext http_tracer_context(*headers);
    TestTraceContextImpl trace_context{};

    const std::string value = "value";
    const std::string text_html = "text/html";

    normal_key.setRef(trace_context, value);
    auto iter = trace_context.context_map_.find("key");
    // setRef make no sense for non-HTTP context.
    EXPECT_NE(iter->first.data(), value.data());

    inline_key.setRef(trace_context, text_html);
    auto iter2 = trace_context.context_map_.find("content-type");
    // setRef make no sense for non-HTTP context.
    EXPECT_NE(iter2->first.data(), text_html.data());

    normal_key.setRef(http_tracer_context, value);
    auto iter3 = headers->get(Http::LowerCaseString("key"));
    // setRef make sense for HTTP context.
    EXPECT_EQ(iter3[0]->key().getStringView().data(), normal_key.key().get().data());
    EXPECT_EQ(iter3[0]->value().getStringView().data(), value.data());

    inline_key.setRef(http_tracer_context, text_html);
    auto iter4 = headers->get(Http::LowerCaseString("content-type"));
    // setRef make sense for inline key of HTTP context, the value will be referenced.
    EXPECT_EQ(iter4[0]->value().getStringView().data(), text_html.data());
  }
}

} // namespace
} // namespace Tracing
} // namespace Envoy
