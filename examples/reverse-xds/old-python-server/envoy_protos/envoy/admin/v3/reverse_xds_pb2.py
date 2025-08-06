# Minimal Reverse xDS protobuf stub for demo purposes

class ListenerReadinessStatus:
    """Minimal ListenerReadinessStatus message implementation"""
    
    def __init__(self):
        self.name = ""
        self.ready = False
        self.local_address = ""
        self.error_message = ""
        
    def SerializeToString(self):
        """Serialize to bytes"""
        import json
        data = {
            "name": self.name,
            "ready": self.ready,
            "local_address": self.local_address,
            "error_message": self.error_message
        }
        return json.dumps(data).encode('utf-8')

class ClusterHealthStatus:
    """Minimal ClusterHealthStatus message implementation"""
    
    def __init__(self):
        self.name = ""
        self.healthy_endpoints = 0
        self.total_endpoints = 0

class ConfigurationSnapshot:
    """Minimal ConfigurationSnapshot message implementation"""
    
    def __init__(self):
        self.timestamp = None
        self.listeners = []
        self.clusters = []


