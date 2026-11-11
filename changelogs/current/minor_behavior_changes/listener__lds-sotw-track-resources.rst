A State-of-the-World (SotW) Listener Discovery Service (LDS) update now removes only the listeners
that were previously received through LDS, instead of every dynamic listener that is not part of the
update. Listeners that other components add are therefore no longer removed when a SotW LDS update
arrives, and a listener whose name differs from its resource name, such as an entry of a filesystem
collection, is no longer drained and added again on every SotW update. This behavior can be reverted
by setting ``envoy.reloadable_features.lds_sotw_track_resources`` to ``false``. Independently of
this runtime guard, LDS now generates the name of a listener that it receives without a name, so
one update can add several such listeners instead of rejecting all but the first as duplicates.
