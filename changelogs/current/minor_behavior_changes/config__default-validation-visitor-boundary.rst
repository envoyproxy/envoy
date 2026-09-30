The server's default protobuf message validation visitor, returned by
``ServerFactoryContext::messageValidationVisitor()``, now switches from the static to the dynamic
visitor as soon as the bootstrap resources have been loaded, rather than when the main dispatch
loop starts.
