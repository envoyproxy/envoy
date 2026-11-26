The bootstrap section of ``/config_dump`` now reports the node's live
:ref:`dynamic context parameters <envoy_v3_api_field_config.core.v3.Node.dynamic_parameters>`,
which bootstrap extensions can set via the context provider and which previously only appeared in
xDS discovery requests.
