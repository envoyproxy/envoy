.. _arch_overview_connection_draining:

Connection Draining
===================

Draining within Envoy exists in three states: not draining, draining, and completed draining.

"Not draining" is fairly simple in that connections are opened, utilized, and closed without
additional consideration for draining. "Draining" is the state in which components may discourage
new connections and signal on existing connections the intent to terminate. "Completed Draining"
is the final state of the drain-process in which components will refuse new connections and
terminate any remaining connections (read more at :ref:`Draining<arch_overview_draining>`).


Drain Heirarchy
---------------

Drain signaling is a tree structure within Envoy, that roughly looks like the following:::

                  root
                   │
            ┌──────┴────────┐
            ▼               ▼
         listener-1      listener-2
            │               │
        ┌───┘          ┌────┴───────────┐
        ▼              ▼                ▼
    filter-chain-1  filter-chain-2   filter-chain-3
                                        │
                                    ┌───┴───┐
                                    ▼       ▼
                                  HCM-1    HCM-2

At the top of the tree is the root drain-manager, under which is the individual listeners,
then the listener's filter-chains, and lastly a filter such as the HTTP Connection Manager.

Draining may be initiated at any point in the tree and the effect will cascade downward. In
our example tree above, if `listener-2` were to begin draining, so would `filter-chain-2`,
`filter-chain-3` and both `HCM` filters. However, `listener-1` and its filter-chain would
remain uneffected. However, if `root` were to drain, perhaps the Envoy instance is shutting
down, then all components will receive cascading signals to begin draining.


Drain Timing
------------

When draining is initiated, drain signals will propagate proactively through the drain tree
(or sub-tree). However, the  propagation may not be immediate. The drain-manager will
spread out signaling within the first 25% of the configured drain-time. So if a drain-time
of 60s is configured for the server, then all components within the drain tree will have
received the signal to *begin* draining within the first 15 seconds. This allows for the
remaining 45s to be used for gracefully draining network traffic.


.. attention::

   Draining behavior prior to 1.20.0 used a lazy, pull-based system for dissiminating drain
   state. This would require listeners and filters to proactively check the state in order to
   react. For example, the HTTP Connection Manager would only look at the drain state when
   writing response headers, so active traffic was necessary for achieve graceful draining.

   The APIs used for lazy drain-state reading are still available, so some components may still
   take a lazy approach to enacting draining.
