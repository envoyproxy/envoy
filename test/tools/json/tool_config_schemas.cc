#include "tool_config_schemas.h"

const std::string Json::ToolSchema::ROUTER_CHECK_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "input": {
      "type" : "array",
      "items" : {
        "type": "object",
        "properties": {
          "expected": { "type": "string" },
          "authority": { "type": "string" },
          "path": { "type": "string" },
          "additional_headers": {
            "type": "array",
            "items" : {
              "type": "object",
              "properties": {
                "name": { "type": "string" },
                "value": { "type": "string" }
              }
            }
          },
          "check": {
            "type": "object",
            "properties" : {
              "name": {"type" : "string", "enum" : ["cluster", "virtual_cluster", "virtual_host"] },
              "rewrite" : {"type" : "string", "enum" : ["host", "path"] },
              "redirect" : {"type" : "string", "enum" : ["path"] },
              "maxItems" : 1
            }
          },
          "method": { "type" : "string", "enum": ["GET", "PUT", "POST"] },
          "random_lb_value" : { "type" : "integer" },
          "ssl" : { "type" : "boolean" },
          "internal" : { "type" : "boolean" }
        },
        "additionalProperties": false,
        "required": ["expected", "authority", "path"]
      }
    }
  }
  )EOF");
