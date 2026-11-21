#!/usr/bin/env python3
"""Hardware-independent camera telemetry packet and acceptance tests."""
import ctypes
import pathlib
import struct
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
SOURCE = HERE.parent / "modules" / "astro" / "astro_telemetry.c"

with tempfile.TemporaryDirectory(prefix="midnight-telem-") as tmp:
    library = pathlib.Path(tmp) / "libtelem.so"
    subprocess.check_call(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                           "-fPIC", "-shared", str(SOURCE), "-o", str(library)])
    api = ctypes.CDLL(str(library))
    api.astro_telemetry_publish.argtypes = [
        ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int32,
        ctypes.c_uint16, ctypes.c_uint16, ctypes.c_uint8]
    api.astro_telemetry_encode.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32]
    buffer = (ctypes.c_uint8 * 32)()

    def get():
        api.astro_telemetry_encode(buffer,12345678)
        data = bytes(buffer)
        assert len(data) == 32
        assert data[:4] == b"MNT1"
        assert data[26:28] == bytes(2)
        return struct.unpack("<4sIIIiHHBB2sI", data)

    assert get()[8] == 0  # no verified solution at boot
    assert get()[10] == 12345678  # camera readout clock
    api.astro_telemetry_publish(55, 2350021, -145123, 340, 12, 1)
    a = get()
    assert a[1:9] == (1, 55, 2350021, -145123, 340, 12, 1, 1), a
    api.astro_telemetry_publish(56, 3600000, 1, 340, 12, 1)
    api.astro_telemetry_publish(56, 2, 900001, 340, 12, 1)
    api.astro_telemetry_publish(56, 2, 1, 340, 7, 1)
    assert get() == a  # bad or insufficient matches cannot alter telemetry
    api.astro_telemetry_publish(95, 42, 900000, 25, 18, 4)
    assert get()[1:9] == (2, 95, 42, 900000, 25, 18, 4, 1)
    print("PASS: 32-byte packet, signed Dec, validity, sequence and rejection")
