#include "source/extensions/filters/http/mcp_json_rest_bridge/trace_context.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using ::nlohmann::json;

TEST(McpTraceContextTest, HandlesRequestWithNoParams) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "some_method"
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "");
  EXPECT_EQ(tc.tracestate(), "");
  EXPECT_EQ(tc.baggage(), "");
}

TEST(McpTraceContextTest, NoMetaInMcp) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "");
  EXPECT_EQ(tc.tracestate(), "");
  EXPECT_EQ(tc.baggage(), "");
}

TEST(McpTraceContextTest, NoTraceHeadersInMcpMetaLayer) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "foo": "bar",
            "baz": "qux"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "");
  EXPECT_EQ(tc.tracestate(), "");
  EXPECT_EQ(tc.baggage(), "");
}

TEST(McpTraceContextTest, AllHeadersInMcpLayer) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "traceparent": "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",
            "tracestate": "key2=value2",
            "baggage": "key3=value3"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01");
  EXPECT_EQ(tc.tracestate(), "key2=value2");
  EXPECT_EQ(tc.baggage(), "key3=value3");
}

TEST(McpTraceContextTest, TraceParentAndTraceStateInMcpLayer) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "traceparent": "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",
            "tracestate": "key2=value2"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01");
  EXPECT_EQ(tc.tracestate(), "key2=value2");
  EXPECT_EQ(tc.baggage(), "");
}

TEST(McpTraceContextTest, TraceParentAndBaggageInMcpLayer) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "traceparent": "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",
            "baggage": "key3=value3"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01");
  EXPECT_EQ(tc.tracestate(), "");
  EXPECT_EQ(tc.baggage(), "key3=value3");
}

TEST(McpTraceContextTest, TraceStateAndBaggageInMcpLayer) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "tracestate": "key2=value2",
            "baggage": "key3=value3"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "");
  EXPECT_EQ(tc.tracestate(), "");
  EXPECT_EQ(tc.baggage(), "key3=value3");
}

TEST(McpTraceContextTest, TraceParentOnlyInMcpLayer) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "traceparent": "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01");
  EXPECT_EQ(tc.tracestate(), "");
  EXPECT_EQ(tc.baggage(), "");
}

TEST(McpTraceContextTest, TraceStateOnlyInMcpLayer) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "tracestate": "key2=value2"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "");
  EXPECT_EQ(tc.tracestate(), "");
  EXPECT_EQ(tc.baggage(), "");
}

TEST(McpTraceContextTest, BaggageOnlyInMcpLayer) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "baggage": "key3=value3"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "");
  EXPECT_EQ(tc.tracestate(), "");
  EXPECT_EQ(tc.baggage(), "key3=value3");
}

TEST(McpTraceContextTest, HandlesRequestWithParamsMetaOfWrongType) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": "not-an-object"
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "");
  EXPECT_EQ(tc.tracestate(), "");
  EXPECT_EQ(tc.baggage(), "");
}

TEST(McpTraceContextTest, HandlesRequestWithTraceParentOfWrongType) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "traceparent": 12345,
            "tracestate": "key2=value2",
            "baggage": "key3=value3"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "");
  EXPECT_EQ(tc.tracestate(), "");
  EXPECT_EQ(tc.baggage(), "key3=value3");
}

TEST(McpTraceContextTest, HandlesRequestWithTraceStateOfWrongType) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "traceparent": "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",
            "tracestate": 12345,
            "baggage": "key3=value3"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01");
  EXPECT_EQ(tc.tracestate(), "");
  EXPECT_EQ(tc.baggage(), "key3=value3");
}

TEST(McpTraceContextTest, HandlesRequestWithBaggageOfWrongType) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "traceparent": "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",
            "tracestate": "key2=value2",
            "baggage": 12345
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01");
  EXPECT_EQ(tc.tracestate(), "key2=value2");
  EXPECT_EQ(tc.baggage(), "");
}

TEST(McpTraceContextTest, InvalidTraceparentInMcpLayer) {
  json request = json::parse(R"json({
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "traceparent": "ff-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",
            "tracestate": "key2=value2",
            "baggage": "key3=value3"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "");
  EXPECT_EQ(tc.tracestate(), "");
  EXPECT_EQ(tc.baggage(), "key3=value3");
}

TEST(McpTraceContextTest, EmptyTraceparentInMcpLayer) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "traceparent": "",
            "tracestate": "key2=value2",
            "baggage": "key3=value3"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "");
  EXPECT_EQ(tc.tracestate(), "");
  EXPECT_EQ(tc.baggage(), "key3=value3");
}

TEST(McpTraceContextTest, InvalidTracestateInMcpLayer) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "traceparent": "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",
            "tracestate": "1key=value",
            "baggage": "key3=value3"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01");
  EXPECT_EQ(tc.tracestate(), "");
  EXPECT_EQ(tc.baggage(), "key3=value3");
}

TEST(McpTraceContextTest, InvalidBaggageInMcpLayer) {
  json request = json::parse(R"json({
   "jsonrpc": "2.0",
   "id": 1,
   "method": "tools/call",
   "params": {
        "name": "echo_service",
        "arguments": {
            "echo": "helloworld"
        },
        "_meta": {
            "traceparent": "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",
            "tracestate": "key2=value2",
            "baggage": "key name=value"
        }
    }
  })json");
  McpTraceContext tc(request);
  EXPECT_EQ(tc.traceparent(), "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01");
  EXPECT_EQ(tc.tracestate(), "key2=value2");
  EXPECT_EQ(tc.baggage(), "");
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
