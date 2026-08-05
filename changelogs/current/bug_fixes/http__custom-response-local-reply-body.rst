Fixed the custom response filter so ``%LOCAL_REPLY_BODY%`` in a local response policy's
``body_format`` receives the existing local reply body when the policy does not configure its own
``body``.
