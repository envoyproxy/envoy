Fixed the postgres_proxy filter forwarding incomplete initial message bytes to upstream when the
message arrives in multiple TCP segments, causing PostgreSQL to reject the connection.
