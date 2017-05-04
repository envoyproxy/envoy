from util import get_blob
from util import true, false

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


def test(writer):
    
    writer.write_test_file(
        'Valid',
        schema='ROUTE_CONFIGURATION_SCHEMA',
        data=get_blob(ROUTE_CONFIGURATION_BLOB),
        throws=False,
    )
