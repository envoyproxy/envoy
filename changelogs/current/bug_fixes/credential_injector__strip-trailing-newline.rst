Fixed a bug where a credential loaded from a file-based generic secret was injected into the
request header verbatim, including any trailing newline commonly present in secret files. Since
HTTP header values cannot contain CR/LF, this produced an invalid header and the request failed.
Trailing CR/LF characters are now stripped from the credential before injection, and a credential
consisting only of CR/LF characters is treated as missing.
