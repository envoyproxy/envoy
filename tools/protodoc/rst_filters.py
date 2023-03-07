def rst_anchor(label):
    """Format a label as an Envoy API RST anchor."""
    return f".. _{label}:\n"


def rst_header(text, style=None):
    """Format RST header.
    """
    style = style or "-"
    return f"{text}\n{style * len(text)}\n"
