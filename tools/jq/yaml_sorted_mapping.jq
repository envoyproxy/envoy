# Render a mapping's keys as sorted, letter-grouped yaml
# `<key>:\n    title: <key>` blocks.
def render_entry: "  \(.):\n    title: \(.)";
def render_mapping:
  keys
  | group_by(.[0:1])
  | map(map(render_entry) | join("\n"))
  | join("\n\n");
