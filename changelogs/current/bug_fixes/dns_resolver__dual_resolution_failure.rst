Fixed the c-ares resolver reporting a successful empty result when one address family returned no records
and the other lookup failed in ``AUTO`` or ``V4_PREFERRED`` mode. Such failures no longer remove existing
hosts from strict DNS clusters.
