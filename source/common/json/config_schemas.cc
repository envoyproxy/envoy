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
         "items": {"$ref" : "#/definitions/filters"}
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

const std::string Json::Schema::RDS_CONFIGURATION_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "properties" : {
      "cluster" : {"type": "string"},
      "route_config_name" : {"type": "string"},
      "refresh_delay_ms" : {
        "type" : "integer",
        "minimum" : 0,
        "exclusiveMinimum" : true
      }
    },
    "required" : ["cluster", "route_config_name"],
    "additionalProperties" : false
  }
  )EOF");

const std::string Json::Schema::HTTP_CONN_NETWORK_FILTER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "definitions" : {
      "status_code" : {
        "type" : "object",
        "properties" : {
          "type" : {
            "type" : "string",
            "enum" : ["status_code"]
          },
          "op" : {
            "type" : "string",
            "enum" : [">=", "="]
          },
          "value" : {
            "type" : "integer",
            "minimum" : 0,
            "maximum" : 599
          },
          "runtime_key" : {"type" : "string"}
        },
        "required" : ["type", "op", "value"],
        "additionalProperties" : false
      },
      "duration" : {
        "type" : "object",
        "properties" : {
          "type" : {
            "type" : "string",
            "enum" : ["duration"]
          },
          "op" : {
            "type" : "string",
            "enum" : [">=", "="]
          },
          "value" : {
            "type" : "integer",
            "minimum" : 0
          },
          "runtime_key" : {"type" : "string"}
        },
        "required" : ["type", "op", "value"],
        "additionalProperties" : false
      },
      "not_healthcheck" : {
        "type" : "object",
        "properties" : {
          "type" : {
            "type" : "string",
            "enum" : ["not_healthcheck"]
          }
        },
        "required" : ["type"],
        "additionalProperties" : false
      },
      "traceable_request" : {
        "type" : "object",
        "properties" : {
          "type" : {
            "type" : "string",
            "enum" : ["traceable_request"]
          }
        },
        "required" : ["type"],
        "additionalProperties" : false
      },
      "runtime" : {
        "type" : "object",
        "properties" : {
          "type" : {
            "type" : "string",
            "enum" : ["runtime"]
          },
          "key" : {"type" : "string"}
        },
        "required" : ["type", "key"],
        "additionalProperties" : false
      },
      "logical_filter" : {
        "type" : "object",
        "properties" : {
          "type" : {
            "type" : "string",
            "enum" : ["logical_and", "logical_or"]
          },
          "filters" : {
            "type" : "array",
            "minItems" : 2,
            "items" : {
              "oneOf" : [
                {"$ref" : "#/definitions/status_code"},
                {"$ref" : "#/definitions/duration"},
                {"$ref" : "#/definitions/not_healthcheck"},
                {"$ref" : "#/definitions/logical_filter"},
                {"$ref" : "#/definitions/traceable_request"},
                {"$ref" : "#/definitions/runtime"}
              ]
            }
          }
        },
        "required" : ["type", "filters"],
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
        "additionalProperties" : false
      }
    },
    "properties" : {
      "codec_type" : {
        "type" : "string",
        "enum" : ["http1", "http2", "auto"]
      },
      "stat_prefix" : {"type" : "string"},
      "rds" : {"type": "object"},
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
          "properties" : {
            "path" : {"type" : "string"},
            "format" : {"type" : "string"},
            "filter" : {
              "type" : "object",
              "oneOf" : [
                {"$ref" : "#/definitions/not_healthcheck"},
                {"$ref" : "#/definitions/status_code"},
                {"$ref" : "#/definitions/duration"},
                {"$ref" : "#/definitions/traceable_request"},
                {"$ref" : "#/definitions/runtime"},
                {"$ref" : "#/definitions/logical_filter"}
              ]
            }
          },
          "required" : ["path"],
          "additionalProperties" : false
        }
      },
      "use_remote_address" : {"type" : "boolean"},
      "generate_request_id" : {"type" : "boolean"}
    },
    "required" : ["codec_type", "stat_prefix", "filters"],
    "additionalProperties" : false
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
    "additionalProperties" : false
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
      "properties": {
        "stat_prefix": {"type" : "string"},
        "route_config": {
          "type": "object",
          "properties": {
            "routes": {
              "type": "array",
              "items": {
                "type": "object",
                "properties": {
                  "cluster": {
                    "type": "string"
                  },
                  "source_ip_list" : {
                    "type": "array",
                    "items": {
                      "type": "string"
                    }
                  },
                  "source_ports": {
                    "type": "string"
                  },
                  "destination_ip_list" : {
                    "type": "array",
                    "items": {
                      "type": "string"
                    }
                  },
                  "destination_ports": {
                    "type": "string"
                  }
                },
                "required": ["cluster"],
                "additionalProperties": false
              },
              "additionalProperties": false
            }
          },
          "additionalProperties": false
        }
      },
      "required": ["stat_prefix", "route_config"],
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
    "additionalProperties" : false
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
        },
        "additionalProperties" : false
      }
    },
    "properties" : {
      "prefix" : {"type" : "string"},
      "path" : {"type" : "string"},
      "cluster" : {"type" : "string"},
      "cluster_header" : {"type" : "string"},
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
    "additionalProperties" : false
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

const std::string Json::Schema::BUFFER_HTTP_FILTER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "properties" : {
      "max_request_bytes" : {"type" : "integer"},
      "max_request_time_s" : {"type" : "integer"}
    },
    "required" : ["max_request_bytes", "max_request_time_s"],
    "additionalProperties" : false
  }
  )EOF");

const std::string Json::Schema::FAULT_HTTP_FILTER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "properties" : {
      "abort": {
        "type" : "object",
        "properties" : {
          "abort_percent" : {
            "type" : "integer",
            "minimum" : 0,
            "maximum" : 100
          },
          "http_status" : {
            "type" : "integer",
            "minimum" : 0,
            "maximum" : 599
          }
        },
        "required" : ["abort_percent", "http_status"],
        "additionalProperties" : false
      },
      "delay" : {
        "type" : "object",
        "properties" : {
          "type" : {
            "type" : "string",
            "enum" : ["fixed"]
          },
          "fixed_delay_percent" : {
            "type" : "integer",
            "minimum" : 0,
            "maximum" : 100
          },
          "fixed_duration_ms" : {
            "type" : "integer",
            "minimum" : 0,
            "exclusiveMinimum" : true
          }
        },
        "required" : ["type", "fixed_delay_percent", "fixed_duration_ms"],
        "additionalProperties" : false
      },
      "upstream_cluster" : {"type" : "string"},
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
      }
    },
    "additionalProperties" : false
  }
  )EOF");

const std::string Json::Schema::HEALTH_CHECK_HTTP_FILTER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "properties" : {
      "pass_through_mode" : {"type" : "boolean"},
      "endpoint" : {"type" : "string"},
      "cache_time_ms" : {"type" : "integer"}
    },
    "required" : ["pass_through_mode", "endpoint"],
    "additionalProperties" : false
  }
  )EOF");

const std::string Json::Schema::RATE_LIMIT_HTTP_FILTER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "properties" : {
      "domain" : {"type" : "string"},
      "stage" : {"type" : "integer"}
    },
    "required" : ["domain"],
    "additionalProperties" : false
  }
  )EOF");

const std::string Json::Schema::ROUTER_HTTP_FILTER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "properties" : {
      "dynamic_stats" : {"type" : "boolean"}
    },
    "additionalProperties" : false
  }
  )EOF");

const std::string Json::Schema::CLUSTER_MANAGER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "definitions" : {
      "sds" : {
        "type" : "object",
        "properties" : {
          "cluster" : {"type" : "object"},
          "refresh_delay_ms" : {
            "type" : "integer",
            "minimum" : 0,
            "exclusiveMinimum" : true
          }
        },
        "required" : ["cluster", "refresh_delay_ms"],
        "additionalProperties" : false
      },
      "cds" : {
        "type" : "object",
        "properties" : {
          "cluster" : {"type" : "object"},
          "refresh_delay_ms" : {
            "type" : "integer",
            "minimum" : 0,
            "exclusiveMinimum" : true
          }
        },
        "required" : ["cluster"],
        "additionalProperties" : false
      }
    },
    "properties" : {
      "clusters" : {
        "type" : "array",
        "items" : {"type": "object"}
      },
      "sds" : {"$ref" : "#/definitions/sds"},
      "local_cluster_name" : {"type" : "string"},
      "outlier_detection" : {
        "type" : "object",
        "properties" : {
          "event_log_path" : {"type" : "string"}
        },
        "additionalProperties" : false
      },
      "cds" : {"$ref" : "#/definitions/cds"}
    },
    "required" : ["clusters"],
    "additionalProperties" : false
  }
  )EOF");

const std::string Json::Schema::TOP_LEVEL_CONFIG_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "definitions" : {
      "driver" : {
        "type" : "object",
        "properties" : {
          "type" : {
            "type" : "string",
            "enum" : ["lightstep"]
          },
          "access_token_file" : {"type" : "string"},
          "config" : {
            "type" : "object",
            "properties" : {
              "collector_cluster" : {"type" : "string"}
            },
            "required": ["collector_cluster"],
            "additionalProperties" : false
          }
        },
        "required" : ["type", "access_token_file", "config"],
        "additionalProperties" : false
      },
      "rate_limit_service" : {
        "type" : "object",
        "properties" : {
          "type" : {
            "type" : "string",
            "enum" : ["grpc_service"]
          },
          "config" : {
            "type" : "object",
            "properties" : {
              "cluster_name" :{"type" : "string"}
            },
            "required" : ["cluster_name"],
            "additionalProperties" : false
          }
        },
        "required" : ["type", "config"],
        "additionalProperties" : false
      }
    },
    "properties" : {
      "listeners" : {
        "type" : "array",
        "items" : {"type" : "object"}
      },
      "admin" : {
        "type" : "object",
        "properties" : {
          "access_log_path" : {"type" : "string"},
          "port" : {"type" : "integer"}
        },
        "required" : ["access_log_path", "port"],
        "additionalProperties" : false
      },
      "cluster_manager" : {"type" : "object"},
      "flags_path" : {"type" : "string"},
      "statsd_local_udp_port" : {"type" : "integer"},
      "statsd_tcp_cluster_name" : {"type" : "string"},
      "stats_flush_interval_ms" : {"type" : "integer"},
      "tracing" : {
        "type" : "object",
        "properties" : {
          "http": {
            "type" : "object",
            "properties" : {
              "driver" : {"$ref" : "#/definitions/driver"}
            },
            "additionalProperties" : false
          }
        }
      },
      "rate_limit_service" : {"$ref" : "#/definitions/rate_limit_service"},
      "runtime" : {
        "type" : "object",
        "properties" : {
          "symlink_root" : {"type" : "string"},
          "subdirectory" : {"type" : "string"},
          "override_subdirectory" : {"type" : "string"}
        },
        "required" : ["symlink_root", "subdirectory"],
        "additionalProperties" : false
      }
    },
    "required" : ["listeners", "admin", "cluster_manager"],
    "additionalProperties" : false
  }
  )EOF");

const std::string Json::Schema::CLUSTER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "definitions" : {
      "health_check_bytes" : {
        "type" : "object",
        "properties" : {
          "binary" : {"type" : "string"}
        },
        "additionalProperties" : false
      },
      "health_check" : {
        "type" : "object",
        "properties" : {
          "type" : {
            "type" : "string",
            "enum" : ["http", "tcp"]
          },
          "timeout_ms" : {
            "type" : "integer",
            "minimum" : 0,
            "exclusiveMinimum" : true
          },
          "interval_ms" : {
            "type" : "integer",
            "minimum" : 0,
            "exclusiveMinimum" : true
          },
          "unhealthy_threshold" : {
            "type" : "integer",
            "minimum" : 0,
            "exclusiveMinimum" : true
          },
          "healthy_threshold" : {
            "type" : "integer",
            "minimum" : 0,
            "exclusiveMinimum" : true
          },
          "path" : {"type" : "string"},
          "send" : {
            "type" : "array",
            "items" : {"$ref" : "#/definitions/health_check_bytes"}
          },
          "receive" : {
            "type" : "array",
            "items" : {"$ref" : "#/definitions/health_check_bytes"}
          },
          "interval_jitter_ms" : {
            "type" : "integer",
            "minimum" : 0,
            "exclusiveMinimum" : true
          },
          "service_name" : {"type" : "string"}
        },
        "required" : ["type", "timeout_ms", "interval_ms", "unhealthy_threshold", "healthy_threshold"],
        "additionalProperties" : false
      },
      "circuit_breakers" : {
        "type" : "object",
        "properties" : {
          "max_connections" : {"type" : "integer"},
          "max_pending_requests" : {"type" : "integer"},
          "max_requests" : {"type" : "integer"},
          "max_retries" : {"type" : "integer"}
        },
        "additionalProperties" : false
      },
      "ssl_context" : {
        "type" : "object",
        "properties" : {
          "alpn_protocols" : {"type" : "string"},
          "cert_chain_file" : {"type" : "string"},
          "private_key_file" : {"type" : "string"},
          "ca_cert_file" : {"type" : "string"},
          "verify_certificate_hash" : {"type" : "string"},
          "verify_subject_alt_name" : {"type" : "string"},
          "cipher_suites" : {"type" : "string"},
          "sni" : {"type" :"string"}
        },
        "additionalProperties" : false
      }
    },
    "properties" : {
      "name" : {"type" : "string"},
      "type" : {
        "type" : "string",
        "enum" : ["static", "strict_dns", "logical_dns", "sds"]
      },
      "connect_timeout_ms" : {
        "type" : "integer",
        "minimum" : 0,
        "exclusiveMinimum" : true
      },
      "lb_type" : {
        "type" : "string",
        "enum" : ["round_robin", "least_request", "random"]
      },
      "hosts" : {
        "type" : "array",
        "items" : {
          "type" : "object",
          "properties" : {
            "url" : {"type" : "string"}
          }
        }
      },
      "service_name" : {"type" : "string"},
      "health_check" : {"$ref" : "#/definitions/health_check"},
      "max_requests_per_connection" : {
        "type" : "integer",
        "minimum" : 0,
        "exclusiveMinimum" : true
      },
      "circuit_breakers" : {
        "type" : "object",
        "properties" : {
          "default" : {"$ref" : "#/definitions/circuit_breakers"},
          "high" : {"$ref" : "#/definitions/circuit_breakers"}
        },
        "additionalProperties" : false
      },
      "ssl_context" : {"$ref" : "#/definitions/ssl_context"},
      "features" : {
        "type" : "string",
        "enum" : ["http2"]
      },
      "http_codec_options" : {"type" : "string"},
      "dns_refresh_rate_ms" : {
        "type" : "integer",
        "minimum" : 0,
        "exclusiveMinimum" : true
      },
      "outlier_detection" : {"type" : "object"}
    },
    "required" : ["name", "type", "connect_timeout_ms", "lb_type"],
    "additionalProperties" : false
  }
  )EOF");

const std::string Json::Schema::CDS_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "properties" : {
      "clusters" : {
        "type" : "array",
        "items" : {"type" : "object"}
      }
    },
    "required" : ["clusters"],
    "additionalProperties" : false
  }
  )EOF");

const std::string Json::Schema::SDS_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "definitions" : {
      "host" : {
        "type" : "object",
        "properties" : {
          "ip_address" : {"type" : "string"},
          "port" : {"type" : "integer"},
          "tags" : {
            "type" : "object",
            "properties" : {
              "az" : {"type" : "string"},
              "canary" : {"type" : "boolean"},
              "load_balancing_weight": {
                "type" : "integer",
                "minimum" : 1,
                "maximum" : 100
              }
            }
          }
        },
        "required" : ["ip_address", "port"]
      }
    },
    "properties" : {
      "hosts" : {
        "type" : "array",
        "items" : {"$ref" : "#/definitions/host"}
      }
    }
  }
  )EOF");
