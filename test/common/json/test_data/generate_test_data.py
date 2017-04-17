#!/usr/bin/env python

import os
import json
import copy

# convenience when dealing with json
true, false = True, False

# valid blobs for test basis
LISTENER_BLOB = {
    "address": "tcp://0.0.0.0:9300",
    "ssl_context": {
        "alpn_protocols": "h2,http/1.1",
        "alt_alpn_protocols": "http/1.1",
        "cert_chain_file": "/etc/cert.pem",
        "private_key_file": "/etc/key.pem"
    },
    "use_proxy_proto": true,
    "filters": []
}

HTTP_CONN_NETWORK_FILTER_BLOB = {
    "idle_timeout_s": 300,
    "stat_prefix": "router",
    "use_remote_address": true,
    "server_name": "envoy-123",
    "access_log": [
        {
            "filter": {
                "type": "logical_and",
                "filters": [
                    {
                        "type": "not_healthcheck"
                    },
                    {
                        "type": "runtime",
                        "key": "access_log.front_access_log"
                    }
                ]
            },
            "path": "/var/log/envoy/access.log"
        },
        {
            "filter": {
                "type": "logical_or",
                "filters": [
                    {
                        "runtime_key": "access_log.access_error.status",
                        "type": "status_code",
                        "value": 500,
                        "op": ">="
                    },
                    {
                        "type": "status_code",
                        "value": 429,
                        "op": "="
                    },
                    {
                        "runtime_key": "access_log.access_error.duration",
                        "type": "duration",
                        "value": 1000,
                        "op": ">="
                    },
                    {
                        "type": "traceable_request"
                    }
                ]
            },
            "path": "/var/log/envoy/access_error.log"
        }
    ],
    "tracing": {
        "request_headers_for_tags": [
            "x-source"
        ],
        "operation_name": "ingress"
    },
    "filters": [
        {
            "config": {
                "endpoint": "/healthcheck",
                "pass_through_mode": false
            },
            "type": "both",
            "name": "health_check"
        },
        {
            "config": {},
            "type": "decoder",
            "name": "router"
        }
    ],
    "route_config": {
        "virtual_hosts": [
            {
                "domains": [
                    "production.example.com"
                ],
                "require_ssl": "all",
                "routes": [
                    {
                        "host_redirect": "example.com",
                        "prefix": "/"
                    }
                ],
                "name": "production_redirect"
            }
        ],
        "internal_only_headers": [
            "x-role",
            "x-source"
        ],
        "response_headers_to_remove": [
            "x-powered-by"
        ]
    },
    "add_user_agent": true,
    "codec_type": "auto"
}

def main():
    write_test_file(
        'Valid',
        schema='LISTENER_SCHEMA',
        data=get_blob(LISTENER_BLOB),
        throws=False,
    )

    write_test_file(
        'Valid',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=get_blob(HTTP_CONN_NETWORK_FILTER_BLOB),
        throws=False,
    )

def get_blob(blob):
    return copy.deepcopy(blob)

def write_test_file(name, schema, data, throws):
    test_filename = 'schematest-%s-%s.json' % (schema, name)
    if os.path.isfile(test_filename):
        raise ValueError('Test with that name and schema already exists: {}'.format(test_filename))
    with open(os.path.join(os.environ['TEST_TMPDIR'], test_filename), 'w+') as fh:
        json.dump(
            {"schema": schema, "throws": throws, "data": data},
            fh,
            indent=True
        )

if __name__ == '__main__':
    main()
