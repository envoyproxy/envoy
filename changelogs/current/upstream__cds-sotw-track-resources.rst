A State-of-the-World (SotW) CDS update now removes only the clusters that were previously received
through that same CDS subscription, instead of every dynamic cluster that is not part of the update.
Clusters added by on-demand CDS (ODCDS) are therefore no longer removed when a SotW CDS update
arrives, which previously caused ``cluster_not_found`` responses until the clusters were
re-discovered. This behavior can be reverted by setting
``envoy.reloadable_features.cds_sotw_track_resources`` to ``false``.
