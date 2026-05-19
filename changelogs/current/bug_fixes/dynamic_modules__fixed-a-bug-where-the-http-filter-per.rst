Fixed a bug where the HTTP filter per-route configuration and the upstream HTTP TCP bridge
configuration did not handle the ``google.protobuf.Struct`` configuration message as the API
definition requires. Both factories now serialize the ``Struct`` to a JSON string and pass the
string to the dynamic module side as the configuration, matching the behavior already in place
for every other dynamic module extension factory.
