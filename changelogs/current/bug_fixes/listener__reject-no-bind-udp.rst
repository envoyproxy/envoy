Fixed a crash at startup when a UDP or QUIC listener was configured with ``bind_to_port: false``.
This combination was never functional and is now rejected at configuration load with a validation
error.
