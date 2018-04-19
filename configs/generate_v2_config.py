#Import necessary functions from Jinja2 module
import jinja2
import yaml
import os
SCRIPT_DIR = os.path.dirname(__file__)

front_envoy_clusters = {
    'service1': {},
    'service2': {},
    'service3': {},
    'ratelimit': {}
}

service_to_service_envoy_clusters = {
    'ratelimit': {},
    'service1': {
        'service_to_service_rate_limit': True
    },
    'service3': {}
}
external_virtual_hosts = [
{
    'name': 'dynamodb_iad',
    'address': "tcp://127.0.0.1:9204",
    'hosts': [
        {
            'name': 'dynamodb_iad', 'domain': '*',
            'remote_address': 'dynamodb.us-east-1.amazonaws.com:443',
            'verify_subject_alt_name': [ 'dynamodb.us-east-1.amazonaws.com' ],
            'ssl': True
        }
    ],
    'is_amzn_service': True,
    'cluster_type': 'logical_dns'
}]
mongos_servers = {
    'somedb': {
        'address': "tcp://127.0.0.1:27019",
        'hosts': [
            "router1.yourcompany.net:27817",
            "router2.yourcompany.net:27817",
            "router3.yourcompany.net:27817",
            "router4.yourcompany.net:27817",
        ],
        'ratelimit': True
    }
}

def generate_v2_config(template_path, template, output_file, **context):
    env = jinja2.Environment(loader=jinja2.FileSystemLoader(template_path, followlinks=True),
                             undefined=jinja2.StrictUndefined, keep_trailing_newline=True)	
    template = env.get_template(template)
    data = template.render(**context)
    print data

# generate_v2_config(SCRIPT_DIR, 'envoy_front_proxy.template.yaml',
                       # './envoy_front_proxy.yaml', clusters=front_envoy_clusters)
generate_v2_config(SCRIPT_DIR, 'envoy_front_proxy_v2.template.yaml',
                        './envoy_double_proxy.yaml')
#
# generate_v2_config("./", 'envoy_service_to_service.template.yaml',
                    # 'envoy_service_to_service.yaml',
                # internal_virtual_hosts=service_to_service_envoy_clusters,
                # external_virtual_hosts=external_virtual_hosts,
                # mongos_servers=mongos_servers)
