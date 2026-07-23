added ``StreamSharingMayImpactPooling::SharedWithDownstreamConnectionOnClose`` for marking
filter state objects that should be reverse-propagated from the upstream (inner-side) to the
downstream (outer-side) connection at upstream close. Currently honored across the
internal-listener boundary by ``PassthroughState`` / ``InternalSocket``.
