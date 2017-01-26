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
         "minItems" : 1,
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

const std::string Json::Schema::CLIENT_SSL_SCHEMA(R"EOF(
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

const std::string Json::Schema::MONGO_PROXY_SCHEMA(R"EOF(
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

const std::string Json::Schema::RATELIMIT_SCHEMA(R"EOF(
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
            "additionalProperties": false
          }
        }
      }
    },
    "required": ["stat_prefix", "descriptors", "domain"],
    "additionalProperties": false
  }
  )EOF");

const std::string Json::Schema::REDIS_PROXY_SCHEMA(R"EOF(
  {
      "$schema": "http://json-schema.org/schema#",
      "properties":{
        "cluster_name" : {"type" : "string"}
      },
      "required": ["cluster_name"],
      "additionalProperties": false
  }
  )EOF");

const std::string Json::Schema::TCP_PROXY_SCHEMA(R"EOF(
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
