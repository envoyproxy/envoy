#include "config_schemas.h"

const std::string Json::Schema::LISTENER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "definitions": {
      "ssl_context" : {
        "type" : "object",
        "properties" : {
          "cert_chain_file" : {"type" : "string"},
          "private_key_file": {"type" : "string"},
          "alpn_protocols" : {"type" : "string"},
          "alt_alpn_protocols": {"type" : "string"},
          "ca_cert_file" : {"type" : "string"},
          "verify_certificate_hash" : {"type" : "string"},
          "verify_subject_alt_name" : {"type" : "string"},
          "cipher_suites" : {"type" : "string"}
        },
        "required": ["cert_chain_file", "private_key_file"],
        "additionalProperties": false
      },
      "filters" : {
        "type" : "object",
        "properties" : {
          "type": {"type" : "string", "enum" : ["read", "write", "both"] },
          "name" : {
            "type": "string"
          },
          "config": {"type" : "object"}
        },
        "required": ["type", "name", "config"],
        "additionalProperties": false
      }
    },
    "properties": {
       "port": {"type": "number"},
       "filters" : {
         "type" : "array",
         "items": {
           "type": "object",
           "properties": {"$ref" : "#/definitions/filters"}
         }
       },
       "ssl_context" : {"$ref" : "#/definitions/ssl_context"},
       "bind_to_port" : {"type": "boolean"},
       "use_proxy_proto" : {"type" : "boolean"},
       "use_original_dst" : {"type" : "boolean"}
    },
    "required": ["port", "filters"],
    "additionalProperties": false
  }
  )EOF");

const std::string Json::Schema::CLIENT_SSL_NETWORK_FILTER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "properties": {
      "auth_api_cluster" : {"type" : "string"},
      "stat_prefix" : {"type" : "string"},
      "ip_white_list" : {
        "type" : "array",
        "minItems" : 1,
        "uniqueItems": true,
        "items" : {
          "type": "string",
          "format" : "ipv4"
        }
      }
    },
    "required": ["auth_api_cluster", "stat_prefix"],
    "additionalProperties": false
  }
  )EOF");

const std::string Json::Schema::HTTP_CONN_NETWORK_FILTER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "definitions" : {
      "access_log": {
        "type" : "object",
        "properties" : {
          "path" : {"type" : "string"},
          "format" : {"type" : "string"},
          "filter" : {"type" : "object"}
        },
        "required" : ["path"],
        "additionalProperties" : false
      },
      "tracing" : {
        "type" : "object",
        "properties" : {
          "operation_name" : {"type" : "string"}
        },
        "required" : ["operation_name"],
        "additionalProperties" : false
      },
      "filters" : {
        "type" : "object",
        "properties" : {
          "type": {
            "type" : "string",
            "enum" : ["encoder", "decoder", "both"]
          },
          "name" : {"type": "string"},
          "config": {"type" : "object"}
        },
        "required": ["type", "name", "config"],
        "additionalProperties": false
      }
    },
    "properties" : {
      "codec_type" : {
        "type" : "string",
        "enum" : ["http1", "http2", "auto"]
      },
      "stat_prefix" : {"type" : "string"},
      "route_config" : {"type": "object"},
      "filters" : {
        "type" : "array",
        "minItems" : 1,
        "properties" : {"$ref" : "#/definitions/filters"}
      },
      "add_user_agent" : {"type" : "boolean"},
      "tracing" : {"$ref" : "#/defintions/tracing"},
      "http_codec_options" : {
        "type" : "string",
        "enum" : ["no_compression"]
      },
      "server_name" : {"type" : "string"},
      "idle_timeout_s" : {"type" : "integer"},
      "drain_timeout_ms" : {"type" : "integer"},
      "access_log" : {
        "type" : "array",
        "items" : {
          "type" : "object",
          "properties" : {"$ref" : "#/definitions/access_log"}
        }
      },
      "use_remote_address" : {"type" : "boolean"},
      "generate_request_id" : {"type" : "boolean"}
    },
    "required" : ["codec_type", "stat_prefix", "route_config", "filters"],
    "additionalProperties": false
  }
  )EOF");

const std::string Json::Schema::MONGO_PROXY_NETWORK_FILTER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "properties":{
      "stat_prefix" : {"type" : "string"},
      "access_log" : {"type" : "string"}
    },
    "required": ["stat_prefix"],
    "additionalProperties": false
  }
  )EOF");

const std::string Json::Schema::RATELIMIT_NETWORK_FILTER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "properties":{
      "stat_prefix" : {"type" : "string"},
      "domain" : {"type" : "string"},
      "descriptors": {
        "type": "array",
        "items" : {
          "type" : "array" ,
          "minItems" : 1,
          "uniqueItems": true,
          "items": {
            "type": "object",
            "properties": {
              "key" : {"type" : "string"},
              "value" : {"type" : "string"}
            },
            "required": ["key", "value"],
            "additionalProperties": false
          }
        }
      }
    },
    "required": ["stat_prefix", "descriptors", "domain"],
    "additionalProperties": false
  }
  )EOF");

const std::string Json::Schema::REDIS_PROXY_NETWORK_FILTER_SCHEMA(R"EOF(
  {
      "$schema": "http://json-schema.org/schema#",
      "properties":{
        "cluster_name" : {"type" : "string"}
      },
      "required": ["cluster_name"],
      "additionalProperties": false
  }
  )EOF");

const std::string Json::Schema::TCP_PROXY_NETWORK_FILTER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "properties":{
      "stat_prefix" : {"type" : "string"},
      "cluster" : {"type" : "string"}
    },
    "required": ["stat_prefix", "cluster"],
    "additionalProperties": false
  }
  )EOF");

const std::string Json::Schema::ROUTE_CONFIGURATION_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "properties":{
      "virtual_hosts" : {"type" : "array"},
      "internal_only_headers" : {
        "type" : "array",
        "items" : {"type" : "string"}
      },
      "response_headers_to_add" : {
        "type" : "array",
        "minItems" : 1,
        "uniqueItems" : true,
        "items" : {
          "type": "object",
          "properties": {
            "key" : {"type" : "string"},
            "value" : {"type" : "string"}
          },
          "required": ["key", "value"],
          "additionalProperties": false
        }
      },
      "response_headers_to_remove" : {
        "type" : "array",
        "items" : {"type" : "string"}
      }
    },
    "required": ["virtual_hosts"],
    "additionalProperties": false
  }
  )EOF");

const std::string Json::Schema::VIRTUAL_HOST_CONFIGURATION_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "definitions" : {
      "virtual_clusters" : {
        "type" : "object" ,
        "properties" : {
          "pattern" : {"type" : "string"},
          "string" : {"type" : "string"},
          "method" : {"type" : "string"},
          "priority" : {"type" : "string"}
        },
        "required" : ["pattern", "name"],
        "additionalProperties" : false
      }
    },
    "properties" : {
      "name" : {"type" : "string"},
      "domains": {
        "type": "array",
          "items": {"type" : "string"}
      },
      "routes": {"type" : "array"},
      "require_ssl" : {
        "type" : "string",
        "enum" : ["all", "external_only"]
      },
      "virtual_clusters" : {
        "type" : "array",
        "minItems" : 1,
        "properties" : {"$ref" : "#/definitions/virtual_clusters"}
      },
      "rate_limits" : {"type" : "array"}
    },
    "required" : ["name", "domains", "routes"],
    "additionalProperties": false
  }
  )EOF");

const std::string Json::Schema::ROUTE_ENTRY_CONFIGURATION_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "definitions" : {
      "weighted_clusters" : {
        "type" : "object",
        "properties" : {
          "clusters" : {
            "type" : "array",
            "items" : {
              "type" : "object",
              "properties" : {
                "name" : {"type" : "string"},
                "weight" : {"type" : "integer"}
              },
              "required" : ["name", "weight"],
              "additionalProperties" : false
            }
          },
          "runtime_key_prefix" : {"type" : "string"}
        }
      }
    },
    "properties" : {
      "prefix" : {"type" : "string"},
      "path" : {"type" : "string"},
      "cluster" : {"type" : "string"},
      "weighted_clusters": {"$ref" : "#/definitions/weighted_clusters"},
      "host_redirect" : {"type" : "string"},
      "path_redirect" : {"type" : "string"},
      "prefix_rewrite" : {"type" : "string"},
      "host_rewrite" : {"type" : "string"},
      "case_sensitive" : {"type" : "boolean"},
      "timeout_ms" : {"type" : "integer"},
      "runtime" : {
        "type" : "object",
        "properties" : {
          "key": {"type" : "string"},
          "default" : {"type" : "integer"}
        },
        "required" : ["key", "default"],
        "additionalProperties" : false
      },
      "retry_policy" : {
        "type" : "object",
        "properties" : {
          "num_retries" : {"type" : "integer"},
          "retry_on" : {"type" : "string"}
        },
        "required" : ["retry_on"],
        "additionalProperties" : false
      },
      "shadow" : {
        "type" : "object",
        "properties" : {
          "cluster" : {"type" : "string"},
          "runtime_key" : {"type": "string"}
        },
        "required" : ["cluster"],
        "additionalProperties" : false
      },
      "priority" : {
        "type" : "string",
        "enum" : ["default", "high"]
      },
      "headers" : {
        "type" : "array",
        "minItems" : 1,
        "items" : {
          "type" : "object",
          "properties" : {
            "name" : {"type" : "string"},
            "value" : {"type" : "string"},
            "regex" : {"type" : "boolean"}
          },
          "required" : ["name"],
          "additionalProperties" : false
        }
      },
      "rate_limits" : {"type" : "array"}
    },
    "additionalProperties": false
  }
  )EOF");

const std::string Json::Schema::HTTP_RATE_LIMITS_CONFIGURATION_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "definitions" : {
      "service_to_service" : {
        "type" : "object",
        "properties" : {
          "type" : {
            "type" : "string",
            "enum" : ["service_to_service"]
          }
        },
        "required" : ["type"],
        "additionalProperties" : false
      },
      "request_headers" : {
        "type" : "object",
        "properties" : {
          "type" : {
            "type" : "string",
            "enum" : ["request_headers"]
          },
          "header_name" : {"type" : "string"},
          "descriptor_key" : {"type" : "string"}
        },
        "required" : ["type", "header_name", "descriptor_key"],
        "additionalProperties" : false
      },
      "remote_address" : {
        "type" : "object",
        "properties" : {
          "type" : {
            "type" : "string",
            "enum" : ["remote_address"]
          }
        },
        "required" : ["type"],
        "additionalProperties" : false
      }
    },
    "properties" : {
      "stage" : {"type" : "integer"},
      "kill_switch_key" : {"type" : "string"},
      "route_key" : {"type" : "string"},
      "actions" : {
        "type" : "array",
        "minItems": 1,
        "items" : {
          "anyOf" : [
            {"$ref" : "#/definitions/service_to_service"},
            {"$ref" : "#/definitions/request_headers"},
            {"$ref" : "#/definitions/remote_address"}
          ]
        }
      }
    },
    "required" : ["actions"],
    "additionalProperties" : false
  }
  )EOF");
