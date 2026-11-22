Fixed the listener manager to notify its listener update callbacks of an update that replaces an
active listener before the workers start. When destroying the replaced listener finished the server
initialization, the workers started first and the update was not reported, so a bootstrap dynamic
module with ``enable_listener_lifecycle`` missed its ``on_listener_add_or_update`` notification.
