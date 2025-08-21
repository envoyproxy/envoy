{{ anchor | rst_anchor }}
{% if orphan -%}

:orphan:

{% endif -%}
{{ title | rst_header(style) }}
{% if extension -%}
{{ extension }}
{% endif -%}
{%- if warnings %}
{{ warnings }}
{% endif -%}
{%- if proto_link %}
{{ proto_link }}
{% endif -%}
{%- if comment %}
{{ comment }}
{%- endif -%}
