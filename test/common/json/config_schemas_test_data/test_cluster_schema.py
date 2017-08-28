from util import get_blob
from util import true, false

CLUSTER_BLOB = {
    "name": "foo", 
    "connect_timeout_ms": 250, 
    "type": "sds", 
    "lb_type": "least_request", 
    "features": "http2",
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


def test(writer):
    
    writer.write_test_file(
        'Valid',
        schema='CLUSTER_SCHEMA',
        data=get_blob(CLUSTER_BLOB),
        throws=False,
    )

    blob = get_blob(CLUSTER_BLOB)
    blob['features'] = "nonexistentfeature"
    writer.write_test_file(
        'UnsupportedFeature',
        schema='CLUSTER_SCHEMA',
        data=blob,
        throws=True,
    )
