Added a Wuffs-backed streaming JSON parser for AI protocol parsing (MCP, A2A, OpenAI, Anthropic, etc.) with the following properties:
incremental chunk-by-chunk parsing that resumes across arbitrary HTTP body chunk boundaries without buffering the full body;
token-based processing with no DOM allocation — heap usage is proportional only to fields the handler opts into capturing, not to the total body size;
AI-native field extraction that inlines scalar fields (e.g. ``model``, ``method``, ``id``, ``params.name``) into small bounded strings and captures large fields (e.g. ``messages[]``, ``params.arguments``) as zero-copy byte-range references into the original body;
and duplicate-key detection that rejects key-smuggling attacks (e.g. multiple ``"model"`` fields) with early mid-stream termination before the full body is consumed.
