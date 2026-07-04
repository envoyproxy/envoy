Fixed a bug where a change to an endpoint's ``hostname`` (or ``health_check_config.hostname``)
delivered via EDS was silently ignored for endpoints whose address was unchanged, leaving the host
with a stale hostname (affecting, for example, ``auto_host_sni``). Such changes now take effect by
recreating the affected host, which also drains and recreates that host's connection pools.
