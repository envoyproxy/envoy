# Minimal Any protobuf stub for demo purposes

class Any:
    """Minimal Any message implementation"""
    
    def __init__(self):
        self.type_url = ""
        self.value = b""
    
    def Pack(self, message):
        """Pack a message into Any"""
        # Simple implementation for demo
        if hasattr(message, 'SerializeToString'):
            self.value = message.SerializeToString()
        else:
            # For dict objects, convert to JSON-like representation
            import json
            self.value = json.dumps(message).encode('utf-8')
        
        # Set type URL based on message type
        if hasattr(message, '__class__'):
            self.type_url = f"type.googleapis.com/{message.__class__.__name__}"
        
        return self
    
    def Unpack(self, message_class):
        """Unpack Any into a message"""
        # Simple implementation for demo
        return message_class()
    
    def SerializeToString(self):
        """Serialize to bytes"""
        import json
        data = {
            "type_url": self.type_url,
            "value": self.value.decode('utf-8') if isinstance(self.value, bytes) else self.value
        }
        return json.dumps(data).encode('utf-8')
    
    @classmethod
    def FromString(cls, data):
        """Deserialize from bytes"""
        import json
        obj = cls()
        parsed = json.loads(data.decode('utf-8'))
        obj.type_url = parsed.get("type_url", "")
        obj.value = parsed.get("value", "").encode('utf-8')
        return obj