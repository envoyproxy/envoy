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
  TraceContextHandler inline_key("content-type"); // This key is inline key for HTTP.

  // Test get.
  {
    auto headers = Http::RequestHeaderMapImpl::create();
    headers->setContentType("text/plain");
    headers->addCopy(Http::LowerCaseString("key"), "value");

    HttpTraceContext http_tracer_context(*headers);
    TestTraceContextImpl trace_context{{"key", "value"}, {"content-type", "text/plain"}};

    EXPECT_EQ("value", normal_key.get(trace_context).value());
    EXPECT_EQ("text/plain", inline_key.get(trace_context).value());

    EXPECT_EQ("value", normal_key.get(http_tracer_context).value());
    EXPECT_EQ("text/plain", inline_key.get(http_tracer_context).value());
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
