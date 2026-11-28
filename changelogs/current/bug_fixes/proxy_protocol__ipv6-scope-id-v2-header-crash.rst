Fixed Envoy exiting when generating a PROXY protocol v2 header for an IPv6 address with a scope id,
such as ``fe80::1%2``. The v2 header is now built from the address bytes held by the address
objects instead of formatting them to a string and parsing them again.
