## Architecture
Redis cache backend provides additional storage backend in addition to already existing memory and local file system backends.

From API point of view, the cache filter points to a cluster. This is the same approach as in the case of xds configuration. The cluster config contains list of endpoints, methods to find them (static or dns), connection parameters  (like connection timeout), etc.

To communicate with a Redis server, a new async client has been added: RedisAsyncClient. It is built on top of TcpAsyncClient.  Encoding data is done using already existing Redis encoder. The received data is decoded using already existing Redis decoder.

To avoid constant opening and closing TCP connections to the Redis server, the connection stays open after it is initially created. Each worker threads has a separate connection to the Redis server, so there will be maximum as many tcp sessions to a particular Redis server as are worker threads. 

Thread Local Storage structure allocated for RedisAsyncClients uses hash map to map cluster_name to RedisAsyncClient.

As the name says, the RedisAsyncClient is of asynchronous architecture. It means that after sending a request to the Redis server, the thread picks up a next job, which may also result in sending another request to the same Redis server. In this situation the new request is queued and sent to the Redis server after the reply to the previous request is received.

Storage design:

 - Entries are stored under 3 keys: `cache-<hash>-headers`, `cache-<hash>-body` and `cache-<hash>-trailers`. The header's entry, in addition to actual headers, also stores the size of the body and info whether trailers are present.
 - Headers' and trailers' entries are stored using protobufs. The format and utilities to encode/decode are reused from local file backend. In the future, that code may be moved to a common directory and reused by redis and local files backends. For now few files have been copied from local files backend directory.
 - The Redis backend has been design to be used by multiple Envoys. This means that 2 or more Envoys may try to fill the cache at the same time. The coordination is pushed to Redis server. The first Envoy which manages to successfully issue the command `set cache-<hash>-headers "" NX EX 30` is the one which will fill the cache. Note that the command does not write any meaningful content to Redis yet, it writes an empty string, just to reserve the right to fill the cache. The `NX` parameter instructs the Redis server to write the content only when `cache-<hash>-headers` does not exist. `EX 30` parameter instructs the Redis to delete the entry after 30 seconds, This is done to recover from errors when an Envoy cannot complete writing to cache (crash, disconnect, etc). In such situation that particular key will be unblocked after 30 seconds. When filling the cache with headers, body and trailer is successful, the 30 seconds expiration limit is removed.
