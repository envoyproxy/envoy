Fixed a crash in the Redis proxy when an upstream sends a reply while no request is outstanding on
the connection, for example a surplus or unsolicited reply. The upstream client read the front of
an empty pending request list. It now drops such a reply, logs it at debug level and keeps the
upstream connection open.
