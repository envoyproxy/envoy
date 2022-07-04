{{ comment }}
{%- if wip_warning %}
{{ wip_warning }}
{%- endif -%}
{%- if extension %}
{{ extension | indent(10) }}
{%- endif -%}
{%- if categories %}
{% for category in categories %}
{{ category }}
{% endfor -%}
{%- endif -%}
