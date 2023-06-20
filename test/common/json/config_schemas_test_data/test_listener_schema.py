from util import get_blob
from util import true, false

LISTENER_BLOB = {
    "address": "tcp://0.0.0.0:9300",
    "ssl_context": {
        "alpn_protocols": "h2,http/1.1",
        "cert_chain_file": "/etc/cert.pem",
        "private_key_file": "/etc/key.pem"
    },
    "use_proxy_proto": true,
    "filters": []
}


def test(writer):

    writer.write_test_file(
        'Valid',
        schema='LISTENER_SCHEMA',
        data=get_blob(LISTENER_BLOB),
        throws=False,
    )
