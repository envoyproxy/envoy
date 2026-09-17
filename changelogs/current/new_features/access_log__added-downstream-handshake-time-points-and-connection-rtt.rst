Added the ``DS_HS_BEG`` (downstream TLS handshake begin, i.e. when the ClientHello was received) and
``DS_HS_END`` (downstream TLS handshake end) time points to the :ref:`%COMMON_DURATION%
<config_access_log_format_common_duration>` access log formatter. These are populated for both TLS
and QUIC downstream connections. Also added the ``%DOWNSTREAM_CX_RTT%`` access log formatter
returning the last measured round trip time of the downstream connection in milliseconds.
