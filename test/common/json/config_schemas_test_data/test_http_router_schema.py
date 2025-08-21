from util import get_blob
from util import true, false

ROUTER_HTTP_FILTER_BLOB = {"dynamic_stats": true}


def test(writer):
    writer.write_test_file(
        'Valid',
        schema='ROUTER_HTTP_FILTER_SCHEMA',
        data=get_blob(ROUTER_HTTP_FILTER_BLOB),
        throws=False,
    )

    writer.write_test_file(
        'ValidDefaults',
        schema='ROUTER_HTTP_FILTER_SCHEMA',
        data={},
        throws=False,
    )
