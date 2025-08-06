# Minimal protobuf stubs for demo purposes
import sys
from typing import List

class Node:
    def __init__(self):
        self.id = ""
        self.cluster = ""
        self.metadata = {}

class DiscoveryRequest:
    def __init__(self):
        self.version_info = ""
        self.node = None
        self.resource_names = []
        self.type_url = ""
        self.response_nonce = ""
        self.error_detail = None

class DiscoveryResponse:
    def __init__(self):
        self.version_info = ""
        self.resources = []
        self.canary = False
        self.type_url = ""
        self.nonce = ""
        self.control_plane = None

class DeltaDiscoveryRequest:
    def __init__(self):
        self.node = None
        self.type_url = ""
        self.resource_names_subscribe = []
        self.resource_names_unsubscribe = []
        self.initial_resource_versions = {}
        self.response_nonce = ""
        self.error_detail = None

class DeltaDiscoveryResponse:
    def __init__(self):
        self.system_version_info = ""
        self.resources = []
        self.type_url = ""
        self.removed_resources = []
        self.nonce = ""
        self.control_plane = None
