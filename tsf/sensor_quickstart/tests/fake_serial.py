# Read from hub TH100032: production wrote no hub version, its TSF-85 fingers say 2.3.1
TH100032_DEVICE_INFO = bytes.fromhex("0103000000544831303030333207eab7"
                                     "0101020301545031323331333007eac7"
                                     "0101020301545031323330353307eacb")


def reply(command, data):
    return bytes([0x9A, 0x00, command, len(data)]) + data


class FakeSerial:
    def __init__(self, answer=b"", noise=b""):
        self.answer = answer
        self.pending = bytearray(noise)
        self.written = bytearray()

    @property
    def in_waiting(self):
        return len(self.pending)

    def reset_input_buffer(self):
        pass

    def write(self, data):
        self.written += data
        self.pending += self.answer

    def flush(self):
        pass

    def read(self, size=1):
        data = bytes(self.pending[:size])
        del self.pending[:size]
        return data
