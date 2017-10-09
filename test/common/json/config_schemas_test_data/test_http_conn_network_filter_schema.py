from util import get_blob
from util import true, false

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
            "name": "health_check"
        },
        {
            "config": {},
            "name": "router"
        }
    ],
    "route_config": {},
    "add_user_agent": true,
    "codec_type": "auto"
}


def test(writer):
    
    writer.write_test_file(
        'Valid',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=get_blob(HTTP_CONN_NETWORK_FILTER_BLOB),
        throws=False,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'][1]['filter']['filters'][0]['op'] = '<'
    writer.write_test_file(
        'FilterOperatorIsNotSupportedLessThan',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'][1]['filter']['filters'][0]['op'] = '<='
    writer.write_test_file(
        'FilterOperatorIsNotSupportedLessThanEqual',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'][1]['filter']['filters'][0]['op'] = '>'
    writer.write_test_file(
        'FilterOperatorIsNotSupportedGreaterThan',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'].append({"path": "/dev/null", "filter": {"type": "unknown"}})
    writer.write_test_file(
        'FilterTypeIsNotSupported',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'].append({"path": "/dev/null", "filter": {"type": "logical_or", "filters": []}})
    writer.write_test_file(
        'LessThanTwoFiltersInListNoneLogicalOrThrows',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'].append({"path": "/dev/null", "filter": {"type": "logical_and", "filters": []}})
    writer.write_test_file(
        'LessThanTwoFiltersInListNoneLogicalAndThrows',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'].append({"path": "/dev/null", "filter": {"type": "logical_or", "filters": [{"type": "not_healthcheck"}]}})
    writer.write_test_file(
        'LessThanTwoFiltersInListOneLogicalOrThrows',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )

    blob = get_blob(HTTP_CONN_NETWORK_FILTER_BLOB)
    blob['access_log'].append({"path": "/dev/null", "filter": {"type": "logical_and", "filters": [{"type": "not_healthcheck"}]}})
    writer.write_test_file(
        'LessThanTwoFiltersInListOneLogicalAndThrows',
        schema='HTTP_CONN_NETWORK_FILTER_SCHEMA',
        data=blob,
        throws=True,
    )
