The router now drops the request body it kept for retries or internal redirects once the body exceeds
the retry buffer limit and retries/redirects are given up, instead of holding it until the stream
ends. This releases the memory early and lets the downstream read resume sooner.
