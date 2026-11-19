Fixed MCP header Base64 decoding so values with an incomplete Base64 wrapper are treated as
literals, and an empty Base64 payload correctly decodes to an empty string.
