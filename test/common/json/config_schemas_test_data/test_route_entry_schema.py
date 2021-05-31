from util import get_blob
from util import true, false

ROUTE_ENTRY_CONFIGURATION_BLOB = {
    "prefix": "/route",
    "cluster": "local_service_grpc",
    "priority": "high"
}


def test(writer):

    writer.write_test_file(
        'Valid',
        schema='ROUTE_ENTRY_CONFIGURATION_SCHEMA',
        data=get_blob(ROUTE_ENTRY_CONFIGURATION_BLOB),
        throws=False,
    )

    blob = {"prefix": "/foo", "cluster": "local_service_grpc", "priority": "foo"}
    writer.write_test_file(
        'InvalidPriority',
        schema='ROUTE_ENTRY_CONFIGURATION_SCHEMA',
        data=blob,
        throws=True,
    )
