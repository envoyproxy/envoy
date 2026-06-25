Added support for reading the ``connection.requested_server_name`` attribute (downstream TLS SNI)
from a dynamic module HTTP filter via ``get_attribute_string``. Returns ``None`` when no SNI was
offered.
