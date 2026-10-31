jwt_authn: when a JWT has no ``kid``, keys whose ``x5t#S256`` (or, failing that, ``x5t``) differs
from the JWT header's are skipped. Keys without a thumbprint are still tried. This behavior can be
reverted by setting runtime guard ``envoy.reloadable_features.jwt_authn_select_key_by_x5t`` to
``false``.
