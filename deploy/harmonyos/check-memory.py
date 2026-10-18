"""Check Windows memory headroom before a HarmonyOS build or test stage."""

import ctypes
import json
import sys


class MemoryStatus(ctypes.Structure):
    _fields_ = [
        ("length", ctypes.c_uint32),
        ("load", ctypes.c_uint32),
        ("total_physical", ctypes.c_uint64),
        ("available_physical", ctypes.c_uint64),
        ("total_pagefile", ctypes.c_uint64),
        ("available_pagefile", ctypes.c_uint64),
        ("total_virtual", ctypes.c_uint64),
        ("available_virtual", ctypes.c_uint64),
        ("available_extended_virtual", ctypes.c_uint64),
    ]


if sys.platform != "win32":
    sys.exit("The HarmonyOS host memory check requires Windows.")

status = MemoryStatus(length=ctypes.sizeof(MemoryStatus))
kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.GlobalMemoryStatusEx.argtypes = [ctypes.POINTER(MemoryStatus)]
kernel.GlobalMemoryStatusEx.restype = ctypes.c_int32
if not kernel.GlobalMemoryStatusEx(ctypes.byref(status)):
    raise ctypes.WinError(ctypes.get_last_error())

minimum = 8 * 1024**3
can_start = min(status.available_physical, status.available_pagefile) >= minimum
print(
    json.dumps(
        {
            "availablePhysicalGiB": round(status.available_physical / 1024**3, 2),
            "commitHeadroomGiB": round(status.available_pagefile / 1024**3, 2),
            "minimumGiB": 8,
            "canStartHeavyStage": can_start,
        }
    )
)
sys.exit(0 if can_start else 2)
