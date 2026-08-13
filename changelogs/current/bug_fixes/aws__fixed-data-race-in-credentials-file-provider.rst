Fixed a data race in the AWS credentials file provider (``CredentialsFileCredentialsProvider``)
that could corrupt the heap and crash Envoy when ``watched_directory`` was configured. With a
watched directory the cached credentials were refreshed on every ``getCredentials()`` call, and
concurrent worker threads wrote the cached ``Credentials`` and ``last_updated_`` members without
synchronization. The cached state is now guarded by a mutex.
