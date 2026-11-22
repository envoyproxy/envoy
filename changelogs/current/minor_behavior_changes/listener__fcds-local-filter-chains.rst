A listener whose Filter Chain Discovery Service (FCDS) configuration has no config source is no
longer rejected. Instead it waits, without a timeout, for an extension, such as a bootstrap
extension, to add its filter chains by name. When Envoy creates such a listener before it starts its
workers, server initialization waits too. A filter chain name is either discovered through FCDS or
added by an extension, and Envoy rejects a listener that requests a name of the other kind. A
listener keeps its names until it is removed and has finished draining, so an update that sets or
clears the config source of a listener must use new filter chain names.
