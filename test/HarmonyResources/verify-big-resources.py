"""Compare Qt AUTORCC byte arrays with final qt_add_big_resources ELF objects."""

import argparse
import hashlib
import json
import re
import struct
from datetime import datetime, timezone
from pathlib import Path


ARRAY_SYMBOLS = {
    "qt_resource_data": "_ZL16qt_resource_data",
    "qt_resource_name": "_ZL16qt_resource_name",
    "qt_resource_struct": "_ZL18qt_resource_struct",
}
COMMON_RESOURCES = {
    "APMResources", "PX4Resources", "InstrumentValueIcons",
    "qgcimages", "qgcresources", "qgroundcontrol",
}
DECLARATION = re.compile(rb"\s*static const unsigned char (qt_resource_(?:data|name|struct))\[\]\s*=\s*\{\s*")
HEX_BYTES = re.compile(rb"(?:\s*0x[0-9a-fA-F]{1,2}\s*,)*\s*")
CHUNK_SIZE = 65536
MAX_LINE_SIZE = 16384


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_at(stream, offset, size):
    stream.seek(offset)
    data = stream.read(size)
    require(len(data) == size, f"Truncated ELF at offset {offset}, need {size} bytes")
    return data


def elf_symbols(path, resource):
    """Resolve ET_REL symbols through their own section, not as file offsets."""
    file_size = path.stat().st_size
    with path.open("rb") as stream:
        header = read_at(stream, 0, 64)
        require(header[:7] == b"\x7fELF\x02\x01\x01", "Expected little-endian ELF64")
        elf_type, machine = struct.unpack_from("<HH", header, 16)
        require(elf_type == 1, "Expected a final relocatable ELF object (ET_REL)")
        require(machine in (62, 183), f"Unsupported ELF architecture: {machine}")
        table_offset = struct.unpack_from("<Q", header, 40)[0]
        entry_size, count = struct.unpack_from("<HH", header, 58)
        require(64 <= entry_size <= 256 and 0 < count < 4096, "Invalid ELF section table")
        require(table_offset + entry_size * count <= file_size, "ELF section table exceeds file")
        sections = []
        for index in range(count):
            section = struct.unpack("<IIQQQQIIQQ", read_at(stream, table_offset + index * entry_size, 64))
            if section[1] != 8:  # SHT_NOBITS has no file contents.
                require(section[4] + section[5] <= file_size, f"ELF section {index} exceeds file")
            sections.append(section)
        symbols = {}
        initializer = "qInitResources_" + resource
        init_names = {initializer, f"_Z{len(initializer)}{initializer}v"}
        wanted = set(ARRAY_SYMBOLS.values()) | init_names
        for section in sections:
            if section[1] != 2:  # SHT_SYMTAB
                continue
            require(section[9] == 24 and section[5] % 24 == 0, "Invalid ELF64 symbol table")
            require(0 < section[6] < count, "Invalid ELF string-table index")
            strings = sections[section[6]]
            require(strings[1] == 3, "Symbol names do not reference an ELF string table")
            for index in range(section[5] // 24):
                name_offset, info, _, section_index, value, size = struct.unpack(
                    "<IBBHQQ", read_at(stream, section[4] + index * 24, 24))
                require(name_offset < strings[5], "Invalid ELF symbol-name offset")
                name_bytes = read_at(stream, strings[4] + name_offset, min(1024, strings[5] - name_offset))
                require(b"\0" in name_bytes, "Unterminated or oversized ELF symbol name")
                name = name_bytes.split(b"\0", 1)[0].decode("ascii")
                if name not in wanted:
                    continue
                require(name not in symbols, f"Duplicate ELF symbol: {name}")
                require(0 < section_index < count, f"Undefined or special-section ELF symbol: {name}")
                target = sections[section_index]
                require(target[1] == 1, f"ELF symbol {name} is not stored in SHT_PROGBITS")
                require(value + size <= target[5], f"ELF symbol {name} exceeds its section")
                symbols[name] = {
                    "name": name, "sectionIndex": section_index,
                    "sectionOffset": target[4], "sectionValue": value,
                    "fileOffset": target[4] + value, "length": size,
                    "symbolType": info & 15, "binding": info >> 4,
                }
        for name in ARRAY_SYMBOLS.values():
            require(name in symbols, f"Missing ELF resource array: {name}")
            require(symbols[name]["symbolType"] == 1, f"Expected object symbol: {name}")
        initializers = [symbols[name] for name in init_names if name in symbols]
        require(len(initializers) == 1, f"Missing or ambiguous defined initializer: {initializer}")
        require(initializers[0]["symbolType"] == 2 and initializers[0]["binding"] in (1, 2)
                and initializers[0]["length"] > 0, "Resource initializer is not a defined global function")
        data = symbols[ARRAY_SYMBOLS["qt_resource_data"]]
        if data["length"] == 8:
            require(read_at(stream, data["fileOffset"], 8) != b"QRC_DATA", "Rejected Qt pass1 QRC_DATA placeholder")
    return machine, symbols, initializers[0]


def stream_cpp_arrays(path, consume):
    """Parse generated C++ one bounded line at a time; emit at most 64 KiB."""
    source_hash = hashlib.sha256()
    seen = set()
    active = None
    pending = bytearray()
    with path.open("rb") as stream:
        line_number = 0
        while raw := stream.readline(MAX_LINE_SIZE):
            line_number += 1
            require(len(raw) < MAX_LINE_SIZE or raw.endswith(b"\n"),
                    f"Generated C++ line exceeds {MAX_LINE_SIZE} bytes: {line_number}")
            source_hash.update(raw)
            line = raw.split(b"//", 1)[0].strip()
            if active is None:
                declaration = DECLARATION.fullmatch(line)
                if declaration:
                    active = declaration[1].decode("ascii")
                    require(active not in seen, f"Duplicate C++ resource array: {active}")
                    seen.add(active)
                continue
            if line == b"};":
                consume(active, bytes(pending))
                pending.clear()
                active = None
                continue
            require(HEX_BYTES.fullmatch(line) is not None,
                    f"Unexpected resource initializer at C++ line {line_number}")
            if line:
                pending.extend(int(token, 16) for token in line.split(b",") if token.strip())
                while len(pending) >= CHUNK_SIZE:
                    consume(active, bytes(pending[:CHUNK_SIZE]))
                    del pending[:CHUNK_SIZE]
    require(active is None, "Unterminated C++ resource array")
    require(seen == set(ARRAY_SYMBOLS), f"Expected all three C++ resource arrays, found {sorted(seen)}")
    return source_hash.hexdigest()


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def compare_resource(cpp_path, object_path, resource):
    machine, symbols, initializer = elf_symbols(object_path, resource)
    comparisons = {}
    for array, symbol_name in ARRAY_SYMBOLS.items():
        comparisons[array] = {
            **symbols[symbol_name], "cppLength": 0, "firstMismatchOffset": None,
            "cppHash": hashlib.sha256(), "objectHash": hashlib.sha256(),
        }
    with object_path.open("rb") as stream:
        def consume(array, expected):
            result = comparisons[array]
            offset = result["cppLength"]
            available = max(0, min(len(expected), result["length"] - offset))
            actual = read_at(stream, result["fileOffset"] + offset, available)
            if expected != actual and result["firstMismatchOffset"] is None:
                result["firstMismatchOffset"] = offset + next(
                    (index for index, (left, right) in enumerate(zip(expected, actual)) if left != right),
                    min(len(expected), len(actual)))
            result["cppHash"].update(expected)
            result["objectHash"].update(actual)
            result["cppLength"] += len(expected)
        source_hash = stream_cpp_arrays(cpp_path, consume)
        for result in comparisons.values():
            # Include an ELF-only tail in its digest when lengths differ.
            offset = result["cppLength"]
            while offset < result["length"]:
                chunk = read_at(stream, result["fileOffset"] + offset, min(CHUNK_SIZE, result["length"] - offset))
                result["objectHash"].update(chunk)
                offset += len(chunk)
            result["cppSha256"] = result.pop("cppHash").hexdigest()
            result["objectSha256"] = result.pop("objectHash").hexdigest()
            result["status"] = "passed" if (
                result["cppLength"] == result["length"] and result["firstMismatchOffset"] is None
                and result["cppSha256"] == result["objectSha256"]) else "failed"
    return {
        "resource": resource, "architecture": {62: "x86_64", 183: "arm64-v8a"}[machine],
        "cpp": str(cpp_path), "cppSha256": source_hash,
        "object": str(object_path), "objectSha256": digest(object_path),
        "initializer": initializer, "arrays": comparisons,
        "status": "passed" if all(item["status"] == "passed" for item in comparisons.values()) else "failed",
    }


def verify_build(build_root):
    objects = {path.stem.removeprefix("qrc_"): path for path in build_root.glob("qrc_*.o")}
    require(set(objects) - COMMON_RESOURCES in ({"utmsp"}, {"utmsp_dummy"})
            and COMMON_RESOURCES <= set(objects), "Expected exactly seven final QGC resource objects")
    results = []
    for name, object_path in sorted(objects.items()):
        matches = list((build_root / "QGroundControl_autogen").glob(f"*/qrc_{name}.cpp"))
        require(len(matches) == 1, f"Expected one old AUTORCC C++ source for {name}, found {len(matches)}")
        results.append(compare_resource(matches[0], object_path, name))
    require(len({item["architecture"] for item in results}) == 1, "Mixed ELF architectures")
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    project_tmp = Path(__file__).resolve().parents[2] / "tmp"
    build_root, output = args.build_root.resolve(), args.output.resolve()
    require(build_root.is_relative_to(project_tmp.resolve()), "Build root must stay in project tmp")
    require(output.is_relative_to(project_tmp.resolve()), "Report must stay in project tmp")
    report = {
        "createdAt": datetime.now(timezone.utc).isoformat(), "buildRoot": str(build_root),
        "checkerSha256": digest(Path(__file__)), "method": "streamed_cpp_bytes_vs_elf_section_relative_symbols",
        "cppLineLimitBytes": MAX_LINE_SIZE, "comparisonChunkBytes": CHUNK_SIZE,
        "status": "failed", "resources": [],
    }
    try:
        report["resources"] = verify_build(build_root)
        report["architecture"] = report["resources"][0]["architecture"]
        report["summary"] = {
            "resources": len(report["resources"]), "arrays": len(report["resources"]) * 3,
            "comparedBytes": sum(array["cppLength"] for item in report["resources"] for array in item["arrays"].values()),
            "failedResources": sum(item["status"] != "passed" for item in report["resources"]),
        }
        report["status"] = "passed" if report["summary"]["failedResources"] == 0 else "failed"
    except (OSError, ValueError, UnicodeError, struct.error) as error:
        report["error"] = str(error)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "output": str(output),
                      "summary": report.get("summary"), "error": report.get("error")}, ensure_ascii=False))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
