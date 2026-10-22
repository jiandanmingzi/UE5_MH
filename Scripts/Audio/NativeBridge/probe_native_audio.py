"""Bounded READ-ONLY code probe for the authorized local Rise audio bridge.

Needs the isolated Capstone in Saved/Audio/RiseIG/NativeBridge/PythonDeps.
Never opens a process with write/create-thread rights or executes game code.
Example: --pid <Rise PID> --rva 0x4dbbac0 --output Saved/Audio/.../probe.json
"""
from __future__ import annotations

import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "Saved/Audio/RiseIG/NativeBridge/PythonDeps"))


def disassemble(data: bytes, address: int) -> dict:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_IMM

    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    instructions, calls = [], []
    for instruction in decoder.disasm(data, address):
        instructions.append({"address": hex(instruction.address), "bytes": instruction.bytes.hex(),
                             "mnemonic": instruction.mnemonic, "operands": instruction.op_str})
        if instruction.mnemonic == "call" and instruction.operands[0].type == CS_OP_IMM:
            calls.append(hex(instruction.operands[0].imm))
        if instruction.mnemonic in ("ret", "int3"):
            break
    return {"bytes": data.hex(), "instructions": instructions, "direct_calls": calls}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--rva", type=lambda value: int(value, 0), action="append", default=[])
    parser.add_argument("--queue-consumers", action="store_true",
                        help="Bounded WwiseManager code-only xrefs to the verified +DA0 request queue")
    parser.add_argument("--audio-backend-callers", action="store_true",
                        help="Bounded WwiseManager calls into the numeric audio backend; code/SEH only")
    parser.add_argument("--audio-object-writers", action="store_true",
                        help="Bounded code-only writes to the verified RequestInfo audio-object slot (+600)")
    parser.add_argument("--size", type=int, default=512)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not ((args.rva or args.queue_consumers or args.audio_backend_callers or args.audio_object_writers) and len(args.rva) <= 16 and 16 <= args.size <= 4096):
        parser.error("Maximum 16 code regions, each 16..4096 bytes")
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / "Saved").resolve()):
        parser.error("Output must remain inside this project's Saved")

    kernel = c.WinDLL("kernel32", use_last_error=True)
    psapi = c.WinDLL("psapi", use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    kernel.ReadProcessMemory.restype = w.BOOL
    kernel.CloseHandle.argtypes = [w.HANDLE]
    psapi.EnumProcessModules.argtypes = [w.HANDLE, c.POINTER(w.HMODULE), w.DWORD, c.POINTER(w.DWORD)]
    psapi.EnumProcessModules.restype = w.BOOL
    psapi.GetModuleFileNameExW.argtypes = [w.HANDLE, w.HMODULE, w.LPWSTR, w.DWORD]
    psapi.GetModuleFileNameExW.restype = w.DWORD
    process = kernel.OpenProcess(0x0410, False, args.pid)  # QUERY_INFORMATION | VM_READ only
    if not process:
        raise c.WinError(c.get_last_error())
    try:
        modules = (w.HMODULE * 1)()
        needed = w.DWORD()
        if not psapi.EnumProcessModules(process, modules, c.sizeof(modules), c.byref(needed)):
            raise c.WinError(c.get_last_error())
        name = c.create_unicode_buffer(32768)
        if not psapi.GetModuleFileNameExW(process, modules[0], name, len(name)):
            raise c.WinError(c.get_last_error())
        image = Path(name.value)
        if image.name.lower() != "monsterhunterrise.exe":
            raise ValueError("Refusing to inspect a process other than MonsterHunterRise.exe")
        base = int(modules[0])

        def read(address: int, size: int) -> bytes:
            buffer = c.create_string_buffer(size)
            count = c.c_size_t()
            if not kernel.ReadProcessMemory(process, address, buffer, size, c.byref(count)) or count.value != size:
                raise c.WinError(c.get_last_error())
            return buffer.raw

        dos = read(base, 64)
        pe = read(base + struct.unpack_from("<I", dos, 60)[0], 264)
        if pe[:4] != b"PE\0\0" or struct.unpack_from("<H", pe, 24)[0] != 0x20B:
            raise ValueError("Not a PE32+ image")
        image_size = struct.unpack_from("<I", pe, 80)[0]
        result = {"pid": args.pid, "image": str(image), "base": hex(base),
                  "size_of_image": image_size, "pe_timestamp": struct.unpack_from("<I", pe, 8)[0],
                  "access": "QUERY_INFORMATION|VM_READ; no writes or code execution", "regions": []}
        with image.open("rb") as stream:
            result["image_sha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
        if args.queue_consumers or args.audio_backend_callers or args.audio_object_writers:
            # Only this already-identified WwiseManager code interval (320 KiB).
            # No writable/object memory is searched or persisted.
            from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM, CS_OP_IMM
            decoder = Cs(CS_ARCH_X86, CS_MODE_64)
            decoder.detail = True
            first, last = (0x4D80000 if args.audio_backend_callers or args.audio_object_writers else 0x4DA0000), 0x4DF0000
            code = read(base + first, last - first)
            exception_rva, exception_size = struct.unpack_from("<II", pe, 24 + 112 + 3 * 8)
            if exception_size > 16 * 1024 * 1024:
                raise ValueError("Unexpected exception table size")
            functions = read(base + exception_rva, exception_size)
            matches, backend, object_writers = [], [], []
            for offset in range(0, len(functions) - 11, 12):
                begin, end, _ = struct.unpack_from("<III", functions, offset)
                if not (first <= begin < end <= last and end - begin <= 16384):
                    continue
                body = code[begin - first:end - first]
                if not args.audio_backend_callers and not args.audio_object_writers and b"\xa0\x0d\x00\x00" not in body:
                    continue
                hits = []
                instructions = list(decoder.disasm(body, base + begin))
                for index, instruction in enumerate(instructions):
                    if any(op.type == CS_OP_MEM and op.mem.disp == 0xDA0 for op in instruction.operands):
                        hits.append({"address": hex(instruction.address), "instruction": instruction.mnemonic + " " + instruction.op_str})
                    if (args.audio_backend_callers and instruction.mnemonic in ("call", "jmp") and
                            instruction.operands[0].type == CS_OP_IMM and
                            0x7300000 <= instruction.operands[0].imm - base < 0x7400000):
                        backend.append({"function_rva": hex(begin), "call_rva": hex(instruction.address-base),
                                        "target_rva": hex(instruction.operands[0].imm-base)})
                    if (args.audio_object_writers and instruction.mnemonic == "mov" and instruction.operands and
                            instruction.operands[0].type == CS_OP_MEM and instruction.operands[0].mem.disp == 0x600):
                        object_writers.append({"function_rva": hex(begin), "write_rva": hex(instruction.address-base),
                            "context": [{"rva": hex(i.address-base), "instruction": i.mnemonic+" "+i.op_str}
                                        for i in instructions[max(0,index-10):index+2]]})
                if hits:
                    matches.append({"function_rva": hex(begin), "end_rva": hex(end), "hits": hits})
            result["queue_consumers"] = matches
            if args.queue_consumers:
                print(json.dumps(matches, indent=2))
            if args.audio_backend_callers:
                result["audio_backend_callers"] = backend
                print(json.dumps(backend, indent=2))
            if args.audio_object_writers:
                result["audio_object_writers"] = object_writers
                print(json.dumps(object_writers, indent=2))
        for rva in args.rva:
            if not (0 <= rva <= image_size - args.size):
                raise ValueError(f"RVA {rva:#x} outside the named game image")
            region = {"rva": hex(rva), **disassemble(read(base + rva, args.size), base + rva)}
            result["regions"].append(region)
            print("REGION", hex(rva))
            for instruction in region["instructions"]:
                print(instruction["address"], instruction["mnemonic"], instruction["operands"])
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print("Report:", output)
    finally:
        kernel.CloseHandle(process)


if __name__ == "__main__":
    main()
