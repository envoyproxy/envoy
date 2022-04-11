Thrift Integration Test Driver
==============================

The code in this package provides `client.py` and `server.py` which
can be used as a thrift client and server pair. Both scripts support
all the Thrift transport and protocol variations that Envoy's Thrift
proxy supports (or will eventually support):

Transports: framed, unframed, header
Protocols: binary, compact, json

The client script can be configured to write its request and the
server's response to a file. The server script can be configured to
return successful responses, IDL-defined exceptions, or server
(application) exceptions.

Envoy's thrift_proxy integration tests use the `generate_fixtures.sh`
script to create request and response files for various combinations
of transport, protocol, service multiplexing. In addition, the
integration tests generate IDL and application exception responses.
The generated data is used with the Envoy's integration test
infrastructure to simulate downstream and upstream connections.
Generated files are used instead of running the client and server
scripts directly to eliminate the need to select a Thrift upstream
server port (or determine its self-selected port).

Regenerating example.thrift
---------------------------

Install the Apache thrift library (from source or a package) so that
the `thrift` command is available. The `generate_bindings.sh` script
will regenerate the Python bindings which are checked into the
repository.
