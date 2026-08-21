Fixed a bug in the TLS inspector's ``JA4`` fingerprint implementation where GREASE values in the
``signature_algorithms`` extension were included in the ``JA4_c`` hash input, contrary to the
`JA4 specification <https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md>`_.
GREASE values in ``signature_algorithms`` are now excluded, matching the treatment already applied
to cipher suites, extension type IDs, and supported versions.
