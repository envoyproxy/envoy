The Rust dynamic-module SDK's ``mock`` feature now uses ``mockall`` 0.15 instead of 0.13. Modules
whose tests pass their own ``mockall`` types, for example a ``Sequence``, to the SDK's mock types
must update their ``mockall`` dependency to 0.15.
