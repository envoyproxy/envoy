Added optional ``session_signing_key`` to the ``mcp_router`` HTTP filter. When set, composite
session IDs minted by the filter carry an HMAC-SHA256 of the session blob and a presented session
whose MAC does not verify is rejected with a 400 error. Previously the session blob was Base64
encoded only, so the ``ENFORCE`` session identity check compared the authenticated request identity
against a subject read out of the client-supplied, editable blob, and did not bind the session to
the authenticated principal. The ENFORCE integration tests were also fixed: the test helper
inserted the subject unencoded, so the parsed subject was always empty and the accept path was
never exercised.
