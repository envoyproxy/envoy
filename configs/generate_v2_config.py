#Import necessary functions from Jinja2 module
from jinja2 import Environment, FileSystemLoader
import yaml
import os
SCRIPT_DIR = os.path.dirname(__file__)

front_envoy_clusters = {
    'service1': {},
    'service2': {},
    'service3': {},
    'ratelimit': {}
}

def generate_v2_config(template_path, template, output_file, **context):
	env = Environment(loader = FileSystemLoader('./'), trim_blocks=True, lstrip_blocks=True)
	template = env.get_template('envoy_front_proxy.template.yaml')
	data = template.render(clusters=front_envoy_clusters)
	# with open(output_file, 'w') as outfile:
		# yaml.dump(data, outfile, default_flow_style=True)
	print data

generate_v2_config(SCRIPT_DIR, 'envoy_front_proxy.template.yaml',
                        './envoy_front_proxy.yaml', clusters=front_envoy_clusters)
generate_v2_config(SCRIPT_DIR, 'envoy_double_proxy.template.yaml',
                        './envoy_double_proxy.yaml')
