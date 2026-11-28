Fixed QUICHE HTTP/3 local SETTINGS when WebTransport is enabled so Envoy advertises draft-13
``SETTINGS_WT_MAX_SESSIONS`` (``0x14e9cd29``) and sets draft-07 max WebTransport sessions to ``1``.
Safari rejects WebTransport without ``SETTINGS_WT_MAX_SESSIONS``; a value greater than ``1`` also
requires ``SETTINGS_WT_INITIAL_MAX_*`` settings QUICHE does not yet send.