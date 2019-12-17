How do I make Envoy fail over to another region during service degradation?
===========================================================================

Envoy uses the concept of
`priorities <arch_overview_load_balancing_priority_levels>` to express
the idea that a certain set of endpoints should be preferred over others.

By putting the preferred endpoints into the lower priority, Envoy will
always select one of these endpoints as long as that priority is sufficiently
available. This means that common failover scenarios can be expressed by
putting the fallback endpoints in a different priority. See the
`priority <arch_overview_load_balancing_priority_levels>` for more information
about this.
