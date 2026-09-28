dynamic modules: fixed a bug where the network and listener filters could reenter a module when an
HTTP callout completed inline, before the async client returned a request handle. The callout
success and failure callbacks now skip an inline completion, which the module already observes
through the callout return code.
