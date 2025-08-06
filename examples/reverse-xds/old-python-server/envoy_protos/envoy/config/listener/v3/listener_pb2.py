# Minimal Listener protobuf stub for demo purposes

class Listener:
    """Minimal Listener message implementation"""
    
    def __init__(self):
        self.name = ""
        self.address = None
        self.filter_chains = []
        self.use_original_dst = False
        
    def SerializeToString(self):
        """Serialize to bytes"""
        import json
        data = {
            "name": self.name,
            "address": self.address.__dict__ if self.address else None,
            "filter_chains": [fc.__dict__ if hasattr(fc, '__dict__') else fc for fc in self.filter_chains],
            "use_original_dst": self.use_original_dst
        }
        return json.dumps(data).encode('utf-8')

class FilterChain:
    """Minimal FilterChain message implementation"""
    
    def __init__(self):
        self.filters = []
        self.filter_chain_match = None

class Filter:
    """Minimal Filter message implementation"""
    
    def __init__(self):
        self.name = ""
        self.typed_config = None


