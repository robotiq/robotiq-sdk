# Read from hub TH100032: production wrote no hub version, its TSF-85 fingers say 2.3.1
TH100032_DEVICE_INFO = bytes.fromhex("0103000000544831303030333207eab7"
                                     "0101020301545031323331333007eac7"
                                     "0101020301545031323330353307eacb")


def reply(command, data):
    return bytes([0x9A, 0x00, command, len(data)]) + data


class FakeSerial:
    """Answers each command with answers[command]; `chunk` caps the bytes a read returns."""

    def __init__(self, answers=None, noise=b"", chunk=None, **_serial_settings):
        self.answers = answers or {}
        self.pending = bytearray(noise)
        self.written = bytearray()
        self.chunk = chunk

    @property
    def in_waiting(self):
        return len(self.pending)

    def reset_input_buffer(self):
        pass

    def reset_output_buffer(self):
        pass

    def write(self, data):
        self.written += data
        self.pending += self.answers.get(data[2], b"")

    def flush(self):
        pass

    def read(self, size=1):
        size = min(size, self.chunk or size)
        data = bytes(self.pending[:size])
        del self.pending[:size]
        return data
