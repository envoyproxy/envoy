Fixed a use-after-free in the Redis cluster ``CLUSTER SLOTS`` discovery. A cluster refresh
(periodic resolve timer or DNS update) that arrived after ``CLUSTER SLOTS`` completed but while the
zone-discovery ``INFO`` requests it triggered were still in flight could start a second discovery,
overwrite the in-flight callbacks and free memory still referenced by the outstanding requests.
