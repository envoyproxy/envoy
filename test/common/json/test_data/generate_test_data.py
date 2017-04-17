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

TOP_LEVEL_CONFIG_BLOB = {
    "listeners": [
        {
            "address": "tcp://127.0.0.1:1234",
            "filters": []
        }
    ],
    "cluster_manager": {
        "clusters": []
    },
    "admin": {
        "access_log_path": "/var/log/envoy/admin_access.log", 
        "address": "tcp://0.0.0.0:9901"
    }, 
    "tracing": {
        "http": {
            "driver": {
                "type": "lightstep",
                "config": {
                    "access_token_file": "/etc/envoy/envoy.cfg",
                    "collector_cluster": "foo"
                }
            }
        }
    }
}

CLUSTER_BLOB = {
    "name": "foo", 
    "connect_timeout_ms": 250, 
    "type": "sds", 
    "lb_type": "least_request", 
    "features": "http2", 
    "http_codec_options": "no_compression", 
    "service_name": "foo", 
    "health_check": {
        "type": "http", 
        "timeout_ms": 2000, 
        "interval_ms": 10000, 
        "interval_jitter_ms": 10000, 
        "unhealthy_threshold": 2, 
        "healthy_threshold": 2, 
        "path": "/healthcheck", 
        "service_name": "foo"
    }, 
    "outlier_detection": {}
}

ROUTE_ENTRY_CONFIGURATION_BLOB = {
    "prefix": "/route",
    "cluster": "local_service_grpc",
    "priority": "high"
}

ROUTE_CONFIGURATION_BLOB = {
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
                },
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
    "route_config": ROUTE_CONFIGURATION_BLOB,
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

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'][1]['filter']['filters'][0]['op'] = '<'
    write_test_file(
        'FilterOperatorIsNotSupportedLessThan',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'][1]['filter']['filters'][0]['op'] = '<='
    write_test_file(
        'FilterOperatorIsNotSupportedLessThanEqual',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'][1]['filter']['filters'][0]['op'] = '>'
    write_test_file(
        'FilterOperatorIsNotSupportedGreaterThan',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'].append({"path": "/dev/null", "filter": {"type": "unknown"}})
    write_test_file(
        'FilterTypeIsNotSupported',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'].append({"path": "/dev/null", "filter": {"type": "logical_or", "filters": []}})
    write_test_file(
        'LessThanTwoFiltersInListNoneLogicalOrThrows',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'].append({"path": "/dev/null", "filter": {"type": "logical_and", "filters": []}})
    write_test_file(
        'LessThanTwoFiltersInListNoneLogicalAndThrows',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'].append({"path": "/dev/null", "filter": {"type": "logical_or", "filters": [{"type": "not_healthcheck"}]}})
    write_test_file(
        'LessThanTwoFiltersInListOneLogicalOrThrows',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'].append({"path": "/dev/null", "filter": {"type": "logical_and", "filters": [{"type": "not_healthcheck"}]}})
    write_test_file(
        'LessThanTwoFiltersInListOneLogicalAndThrows',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    write_test_file(
        'Valid',
        schema='ROUTE_CONFIGURATION_SCHEMA',
        data=get_blob(ROUTE_CONFIGURATION_BLOB),
        throws=False,
    )

    write_test_file(
        'Valid',
        schema='ROUTE_ENTRY_CONFIGURATION_SCHEMA',
        data=get_blob(ROUTE_ENTRY_CONFIGURATION_BLOB),
        throws=False,
    )

    blob = {"prefix": "/foo", "cluster": "local_service_grpc", "priority": "foo"}
    write_test_file(
        'InvalidPriority',
        schema='ROUTE_ENTRY_CONFIGURATION_SCHEMA',
        data=blob,
        throws=True,
    )

    write_test_file(
        'Valid',
        schema='CLUSTER_SCHEMA',
        data=get_blob(CLUSTER_BLOB),
        throws=False,
    )

    blob = get_blob(CLUSTER_BLOB)
    blob['features'] = "nonexistentfeature"
    write_test_file(
        'UnsupportedFeature',
        schema='CLUSTER_SCHEMA',
        data=blob,
        throws=True,
    )

    write_test_file(
        'Valid',
        schema='TOP_LEVEL_CONFIG_SCHEMA',
        data=get_blob(TOP_LEVEL_CONFIG_BLOB),
        throws=False,
    )

    blob = get_blob(TOP_LEVEL_CONFIG_BLOB)
    blob['tracing']['http']['driver']['type'] = 'unknown'
    write_test_file(
        'UnsupportedTracingDriver',
        schema='TOP_LEVEL_CONFIG_SCHEMA',
        data=blob,
        throws=True,
    )

    
def get_blob(blob):
    return copy.deepcopy(blob)

def write_test_file(name, schema, data, throws):
    test_filename = os.path.join(
        os.environ['TEST_TMPDIR'],
        'schematest-%s-%s.json' % (schema, name)
    )
    if os.path.isfile(test_filename):
        raise ValueError('Test with that name and schema already exists: {}'.format(test_filename))

    with open(test_filename, 'w+') as fh:
        json.dump(
            {"schema": schema, "throws": throws, "data": data},
            fh,
            indent=True
        )

if __name__ == '__main__':
    main()
