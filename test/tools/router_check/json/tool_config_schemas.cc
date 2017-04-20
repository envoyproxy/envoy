#include "test/tools/router_check/json/tool_config_schemas.h"

#include <string>

const std::string& Json::ToolSchema::routerCheckSchema() {
  static const std::string* router_check_schema = new std::string(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
      "type": "array",
      "minItems": 1,
      "items": {
        "type": "object",
        "properties": {
          "test_name": {"type": "string"},
          "input": {
            "type": "object",
            "properties": {
              ":authority": {"type": "string", "minLength": 1},
              ":path": {"type": "string", "minLength": 1},
              ":method": {"type": "string", "enum": ["GET", "PUT", "POST"]},
              "random_value": {"type": "integer"},
              "ssl": {"type": "boolean"},
              "internal": {"type": "boolean"},
              "additional_headers": {
                "type": "array",
                "items": {
                  "type": "object",
                  "properties": {
                    "field": {"type": "string"},
                    "value": {"type": "string"}
                  },
                  "additionalProperties": false,
                  "required": ["field", "value"],
                  "maxProperties": 2
                }
              },
              "additionalProperties": false,
              "required": [":authority", ":path"]
            }
          },
          "validate": {
            "type": "object",
            "properties": {
              "cluster_name": {"type": "string"},
              "virtual_cluster_name": {"type": "string"},
              "virtual_host_name": {"type": "string"},
              "host_rewrite": {"type": "string"},
              "path_rewrite": {"type": "string"},
              "path_redirect": {"type": "string"},
              "header_fields": {
                "type": "array",
                "items": {
                  "type": "object",
                  "properties": {
                    "field": {"type": "string"},
                    "value": {"type": "string"}
                  },
                 "additionalProperties": false,
                 "required": ["field", "value"],
                 "maxProperties": 2
                }
              }
            },
            "minProperties": 1,
            "additionalProperties": false
          }
        },
        "additionalProperties": false,
        "required": ["test_name", "input", "validate"]
      }
    }
  )EOF");

  return *router_check_schema;
}
