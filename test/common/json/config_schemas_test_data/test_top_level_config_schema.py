from util import get_blob
from util import true, false

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


def test(writer):
    
    writer.write_test_file(
        'Valid',
        schema='TOP_LEVEL_CONFIG_SCHEMA',
        data=get_blob(TOP_LEVEL_CONFIG_BLOB),
        throws=False,
    )

    blob = get_blob(TOP_LEVEL_CONFIG_BLOB)
    blob['tracing']['http']['driver']['type'] = 'unknown'
    writer.write_test_file(
        'UnsupportedTracingDriver',
        schema='TOP_LEVEL_CONFIG_SCHEMA',
        data=blob,
        throws=True,
    )

