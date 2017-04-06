#include "test/tools/router_check/json/tool_config_schemas.h"

const std::string Json::ToolSchema::ROUTER_CHECK_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
      "type": "array",
      "minItems": 1,
      "items": {
        "type": "object",
        "properties": {
          "authority": {"type": "string"},
          "path": {"type": "string"},
          "additional_headers": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "name": {"type": "string"},
                "value": {"type": "string"}
              },
              "additionalProperties": false,
              "required": ["name", "value"],
              "maxProperties": 2
            }
          },
          "method": {"type": "string", "enum": ["GET", "PUT", "POST"]},
          "random_value": {"type": "integer"},
          "ssl": {"type": "boolean"},
          "internal": {"type": "boolean"},
          "check": {
            "type": "object",
            "properties": {
              "cluster_name": {"type": "string"},
              "virtual_cluster_name": {"type": "string"},
              "virtual_host_name": {"type": "string"},
              "path_rewrite": {"type": "string"},
              "host_rewrite": {"type": "string"},
              "path_redirect": {"type": "string"}
            },
            "minProperties": 1,
            "additionalProperties": false
          }
        },
        "additionalProperties": false,
        "required": ["authority", "path", "check"]
      }
    }
  )EOF");
