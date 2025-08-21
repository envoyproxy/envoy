from pygments.styles import get_style_by_name
from pygments.token import Generic

ENVOY_STYLES = {
    Generic.Output: "#777777",
}

TangoStyle = get_style_by_name("tango")


class EnvoyCodeStyle(TangoStyle):
    default_style = "tango"
    highlight_color = "#f6f5db"
    styles = TangoStyle.styles.copy()
    styles.update(ENVOY_STYLES)
