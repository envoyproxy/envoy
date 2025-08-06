# Minimal Address protobuf stub for demo purposes

class Address:
    """Minimal Address message implementation"""
    
    def __init__(self):
        self.socket_address = None
        self.pipe = None
        
class SocketAddress:
    """Minimal SocketAddress message implementation"""
    
    def __init__(self):
        self.protocol = "TCP"
        self.address = ""
        self.port_value = 0
        self.named_port = ""
        self.resolver_name = ""
        
    def SerializeToString(self):
        """Serialize to bytes"""
        import json
        data = {
            "protocol": self.protocol,
            "address": self.address,
            "port_value": self.port_value
        }
        return json.dumps(data).encode('utf-8')


