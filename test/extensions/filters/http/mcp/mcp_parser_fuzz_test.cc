#include <string>

#include "source/extensions/filters/http/mcp/mcp_json_parser.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {
namespace {

// Fuzz the MCP JSON parser with various inputs
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  // Limit input size to prevent OOM
  static constexpr size_t kMaxInputSize = 1024 * 1024; // 1MB
  if (len > kMaxInputSize) {
    return;
  }

  std::string input(reinterpret_cast<const char*>(buf), len);

  // Test with default configuration
  {
    McpParserConfig config = McpParserConfig::createDefault();
    McpJsonParser parser(config);

    // Parse the input - expect it not to crash
    auto status = parser.parse(input);
    ENVOY_LOG_MISC(debug, "status {}", status.message());

    // Exercise the various getter methods
    parser.isValidMcpRequest();
    parser.getMethod();
    parser.metadata();

    // Try to get nested values with various paths
    parser.getNestedValue("jsonrpc");
    parser.getNestedValue("method");
    parser.getNestedValue("params");
    parser.getNestedValue("params.name");
    parser.getNestedValue("params.uri");
    parser.getNestedValue("params.arguments");
    parser.getNestedValue("id");
  }

  // Test with custom configuration for various methods
  {
    McpParserConfig custom_config;

    // Add various method configurations
    std::vector<McpParserConfig::AttributeExtractionRule> tools_rules = {
        McpParserConfig::AttributeExtractionRule("params.name"),
        McpParserConfig::AttributeExtractionRule("params.arguments"),
    };
    custom_config.addMethodConfig("tools/call", tools_rules);

    std::vector<McpParserConfig::AttributeExtractionRule> resources_rules = {
        McpParserConfig::AttributeExtractionRule("params.uri"),
    };
    custom_config.addMethodConfig("resources/read", resources_rules);

    McpJsonParser custom_parser(custom_config);

    // Test partial parsing - split input into chunks
    if (len > 0) {
      size_t chunk_size = std::max(size_t(1), len / 3);
      size_t offset = 0;

      while (offset < len) {
        size_t current_chunk = std::min(chunk_size, len - offset);
        auto chunk = input.substr(offset, current_chunk);

        auto status = custom_parser.parse(chunk);
        if (!status.ok()) {
          break;
        }

        offset += current_chunk;

        // Check if early termination occurred
        if (custom_parser.isAllFieldsCollected()) {
          break;
        }
      }
    }
  }

  // Test with extremely nested JSON if input suggests it
  if (len > 10 && input.find('{') != std::string::npos) {
    // Create a config that tries to extract deeply nested fields
    McpParserConfig deep_config;
    std::vector<McpParserConfig::AttributeExtractionRule> deep_rules;

    // Generate nested paths
    for (int depth = 1; depth <= 10; ++depth) {
      std::string path = "params";
      for (int i = 0; i < depth; ++i) {
        path += ".nested";
      }
      path += ".value";
      deep_rules.push_back(McpParserConfig::AttributeExtractionRule(path));
    }

    deep_config.addMethodConfig("deep/test", deep_rules);

    McpJsonParser deep_parser(deep_config);
    auto status = deep_parser.parse(input);
    status = deep_parser.finishParse();
    ENVOY_LOG_MISC(debug, "status {}", status.message());
  }
}

} // namespace
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
