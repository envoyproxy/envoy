Fixed a bug in on-demand xDS where a resource requested after the initial subscription could be
silently dropped instead of delivered. An on-demand update only updated the subscription sent to
the management server, not the internal watch routing, so the server's response for the requested
resource matched no watch and was discarded. This affected on-demand cluster discovery (ODCDS) when
requesting a second, different cluster, and VHDS virtual hosts under a route configuration.
On-demand requests now also register watch routing, so these responses are delivered.
