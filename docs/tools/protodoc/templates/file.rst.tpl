{{ header }}
{{ warnings }}
{%- if comment %}
{{ comment }}
{%- endif -%}
{%- for msg in msgs %}
{{ msg }}
{% endfor %}
{%- for enum in enums %}
{{ enum }}
{% endfor %}
