Fixed a bug where subsequent HTTP filters in the chain did not have access to up-to-date
cluster information after successful on-demand cluster discovery. The route cluster is
now automatically refreshed.
