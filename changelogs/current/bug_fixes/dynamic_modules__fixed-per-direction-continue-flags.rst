Fixed a bug in the HTTP filter where the decode and encode directions shared a single continue
flag, so a request body that continued the decode direction while an upstream response was held at
``encodeHeaders`` turned the later ``continueEncoding()`` into a silent no-op and wedged the held
response. Because the flag was shared, one resume could never continue both directions. The decode
and encode continue states are now tracked independently so a continue in one direction never
suppresses a resume in the other.
