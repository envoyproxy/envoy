.. _install_sandboxes_tls:

TLS
===

This example walks through some of the ways that Envoy can be configured to make
use of encrypted connections using ``TLS``.

It demonstrates a number of commonly used patterns:

- `https` termination proxying to an upstream `http` service
- `https` termination proxying to an upstream `https` service using ``TLS``
- `http` endpoint proxying to an upstream `https` service using ``TLS``


.. include:: _include/docker-env-setup.rst

Step 3: Build the sandbox
*************************
