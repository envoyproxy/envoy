#include <string>

#include "source/extensions/filters/http/mcp/mcp_json_parser.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {
namespace {

// Fuzz test for MCP JSON parser
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  // Create a string view from the fuzz input
  absl::string_view input(reinterpret_cast<const char*>(buf), len);

  // Test with default configuration
  {
    McpParserConfig config = McpParserConfig::createDefault();
    McpJsonParser parser(config);

    // Try parsing the input
    auto parse_status = parser.parse(input);
    
    // If parsing succeeded, try finishing the parse
    if (parse_status.ok()) {
      auto finish_status = parser.finishParse();
      
      // If parsing completed successfully, validate MCP state
      if (finish_status.ok()) {
        // Check validity
        bool is_valid = parser.isValidMcpRequest();
        
        // Try to access various fields
        std::string method = parser.getMethod();
        const auto& metadata = parser.metadata();
        
        // Test nested value access with various paths
        const char* test_paths[] = {
          "jsonrpc",
          "method",
          "params",
          "params.name",
          "params.uri",
          "params.cursor",
          "params.level",
          "params.protocolVersion",
          "params.clientInfo.name",
          "params.arguments",
          "id"
        };
        
        for (const auto* path : test_paths) {
          const auto* value = parser.getNestedValue(path);
          // Just access the value, don't assert anything
          if (value != nullptr) {
            // Try accessing different value types
            if (value->has_string_value()) {
              auto str = value->string_value();
            } else if (value->has_number_value()) {
              auto num = value->number_value();
            } else if (value->has_bool_value()) {
              auto b = value->bool_value();
            } else if (value->has_null_value()) {
              // null value
            }
          }
        }
        
        // Get missing required fields
        const auto& missing = parser.getMissingRequiredFields();
      }
    }
    
    // Test reset and reparse
    parser.reset();
    parser.parse(input);
  }

  // Test with custom configuration
  {
    McpParserConfig custom_config;
    
    // Add various field extraction rules
    std::vector<McpParserConfig::FieldRule> rules = {
      McpParserConfig::FieldRule("params.test1"),
      McpParserConfig::FieldRule("params.test2.nested"),
      McpParserConfig::FieldRule("params.test3.deeply.nested.field"),
      McpParserConfig::FieldRule("id"),
      McpParserConfig::FieldRule("extra.field")
    };
    
    // Add method configurations
    custom_config.addMethodConfig("test/method", rules);
    custom_config.addMethodConfig("fuzz/test", rules);
    
    McpJsonParser custom_parser(custom_config);
    
    // Parse with custom config
    auto status = custom_parser.parse(input);
    if (status.ok()) {
      custom_parser.finishParse();
      custom_parser.isValidMcpRequest();
      custom_parser.getMethod();
    }
  }

  // Test streaming parse with split input
  if (len >= 2) {
    McpParserConfig config = McpParserConfig::createDefault();
    McpJsonParser streaming_parser(config);
    
    // Split input into multiple chunks
    size_t chunk1_size = len / 2;
    size_t chunk2_size = len - chunk1_size;
    
    absl::string_view chunk1(reinterpret_cast<const char*>(buf), chunk1_size);
    absl::string_view chunk2(reinterpret_cast<const char*>(buf + chunk1_size), chunk2_size);
    
    // Parse in chunks
    auto status1 = streaming_parser.parse(chunk1);
    if (status1.ok()) {
      auto status2 = streaming_parser.parse(chunk2);
      if (status2.ok()) {
        streaming_parser.finishParse();
      }
    }
  }

  // Test with multiple random splits
  if (len >= 3) {
    McpParserConfig config = McpParserConfig::createDefault();
    McpJsonParser multi_chunk_parser(config);
    
    // Parse in 3 chunks at random positions
    size_t split1 = len / 3;
    size_t split2 = 2 * len / 3;
    
    absl::string_view part1(reinterpret_cast<const char*>(buf), split1);
    absl::string_view part2(reinterpret_cast<const char*>(buf + split1), split2 - split1);
    absl::string_view part3(reinterpret_cast<const char*>(buf + split2), len - split2);
    
    auto s1 = multi_chunk_parser.parse(part1);
    if (s1.ok()) {
      auto s2 = multi_chunk_parser.parse(part2);
      if (s2.ok()) {
        auto s3 = multi_chunk_parser.parse(part3);
        if (s3.ok()) {
          multi_chunk_parser.finishParse();
        }
      }
    }
  }

  // Test edge cases with empty and single-byte inputs
  {
    McpParserConfig config = McpParserConfig::createDefault();
    McpJsonParser edge_parser(config);
    
    // Empty input
    edge_parser.parse("");
    edge_parser.finishParse();
    
    // Reset and test single byte
    if (len > 0) {
      edge_parser.reset();
      absl::string_view single_byte(reinterpret_cast<const char*>(buf), 1);
      edge_parser.parse(single_byte);
      edge_parser.finishParse();
    }
  }

  // Test parser behavior with early termination
  {
    McpParserConfig term_config = McpParserConfig::createDefault();
    McpJsonParser term_parser(term_config);
    
    auto status = term_parser.parse(input);
    
    // Check if all fields were collected (early termination)
    bool all_collected = term_parser.isAllFieldsCollected();
    
    // Even if early terminated, should be able to finish parse
    if (status.ok()) {
      term_parser.finishParse();
    }
  }
}

} // namespace
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
