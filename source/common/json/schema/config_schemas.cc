#include "config_schemas.h"

const std::string Json::LISTENER_SCHEMA(R"EOF(
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
          "type": {"type" : "string", "enum" :["read", "write", "both"]},
          "name" : {
            "type": "string",
            "enum" : ["client_ssl_auth", "echo", "mongo_proxy", "ratelimit", "tcp_proxy", "http_connection_manager"]
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
