# Minimal Status protobuf stub for demo purposes

class Status:
    """Minimal Status message implementation"""
    
    def __init__(self):
        self.code = 0
        self.message = ""
        self.details = []
    
    def SerializeToString(self):
        """Serialize to bytes"""
        import json
        data = {
            "code": self.code,
            "message": self.message,
            "details": self.details
        }
        return json.dumps(data).encode('utf-8')
    
    @classmethod
    def FromString(cls, data):
        """Deserialize from bytes"""
        import json
        obj = cls()
        parsed = json.loads(data.decode('utf-8'))
        obj.code = parsed.get("code", 0)
        obj.message = parsed.get("message", "")
        obj.details = parsed.get("details", [])
        return obj