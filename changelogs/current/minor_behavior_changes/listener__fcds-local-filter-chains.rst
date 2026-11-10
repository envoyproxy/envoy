A listener whose Filter Chain Discovery Service (FCDS) configuration has no config source is no
longer rejected. Instead it waits, without a timeout, for an extension to add its filter chains by
name. When Envoy creates such a listener before it starts its workers, server initialization waits
too. Envoy keeps the names that FCDS discovers apart from the names that extensions add or that
such listeners wait for, and rejects a listener that would mix them. A listener holds its names
until it is gone, also while it drains, so moving a listener between the two needs new filter chain
names.
