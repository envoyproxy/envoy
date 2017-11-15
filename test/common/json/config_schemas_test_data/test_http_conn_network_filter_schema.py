from util import get_blob
from util import true, false

HTTP_CONN_NETWORK_FILTER_BLOB = {
    "idle_timeout_s": 300,
    "stat_prefix": "router",
    "use_remote_address": true,
    "server_name": "envoy-123",
    "access_log": [],
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
