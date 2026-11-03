class AgoraError(Exception):
    """A server or protocol failure; credentials are never included."""
    def __init__(self, message, *, code=None, status=None):
        super().__init__(message)
        self.code, self.status = code, status

class MFARequired(AgoraError):
    def __init__(self, challenge):
        super().__init__("MFA verification is required", code="MFA_REQUIRED")
        self.challenge = challenge

class OutcomeUnknown(AgoraError):
    """The receipt is not yet known; don't submit the same edit as a new operation."""

class Disconnected(AgoraError):
    pass
