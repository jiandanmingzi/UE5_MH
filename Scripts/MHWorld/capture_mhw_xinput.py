"""Record raw XInput controller state and publish an atomic live sample.

This companion is optional. It reads controller state through Windows XInput,
does not inspect or modify Monster Hunter: World memory, and writes under the
project Saved directory. The Lua bridge path is intentionally fixed to match
the project recorder's configured path.
"""

from __future__ import annotations

import argparse
import csv
import ctypes
from ctypes import wintypes
from datetime import datetime
import msvcrt
import os
from pathlib import Path
import sys
import time
import uuid


PROJECT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_DIR = PROJECT / "Saved/AssetProbe/DescendingThrust/XInput"
DEFAULT_STATE_FILE = DEFAULT_OUTPUT_DIR / "live_state.pipe"
DEFAULT_LOCK_FILE = DEFAULT_OUTPUT_DIR / "capture.lock"
STATE_MAGIC = "MHGZ_XINPUT_V1"
ERROR_SUCCESS = 0
XINPUT_GAMEPAD = 0x0001


class XInputGamepad(ctypes.Structure):
    _fields_ = [
        ("wButtons", wintypes.WORD),
        ("bLeftTrigger", ctypes.c_ubyte),
        ("bRightTrigger", ctypes.c_ubyte),
        ("sThumbLX", ctypes.c_short),
        ("sThumbLY", ctypes.c_short),
        ("sThumbRX", ctypes.c_short),
        ("sThumbRY", ctypes.c_short),
    ]


class XInputState(ctypes.Structure):
    _fields_ = [("dwPacketNumber", wintypes.DWORD), ("Gamepad", XInputGamepad)]


def normalize_axis(value: int) -> float:
    """Normalize signed XInput stick values without applying a dead zone."""
    if value < 0:
        return max(-1.0, value / 32768.0)
    return min(1.0, value / 32767.0)


class XInputPoller:
    def __init__(self) -> None:
        if os.name != "nt":
            raise OSError("XInput capture is supported only on Windows.")
        errors = []
        for dll_name in ("xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"):
            try:
                library = ctypes.WinDLL(dll_name)
                function = library.XInputGetState
                function.argtypes = [wintypes.DWORD, ctypes.POINTER(XInputState)]
                function.restype = wintypes.DWORD
                self.library = library
                self.get_state = function
                self.dll_name = dll_name
                break
            except (OSError, AttributeError) as error:
                errors.append(f"{dll_name}: {error}")
        else:
            raise OSError("Unable to load XInputGetState: " + "; ".join(errors))

    def poll(self, requested_index: int | None) -> dict[str, int | float | bool | None]:
        indices = [requested_index] if requested_index is not None else range(4)
        for index in indices:
            state = XInputState()
            result = self.get_state(index, ctypes.byref(state))
            if result != ERROR_SUCCESS:
                continue
            pad = state.Gamepad
            raw = {
                "lx_raw": int(pad.sThumbLX), "ly_raw": int(pad.sThumbLY),
                "rx_raw": int(pad.sThumbRX), "ry_raw": int(pad.sThumbRY),
            }
            return {
                "controller_index": int(index), "connected": True,
                "packet_number": int(state.dwPacketNumber), "buttons": int(pad.wButtons),
                "left_trigger_raw": int(pad.bLeftTrigger),
                "right_trigger_raw": int(pad.bRightTrigger),
                **raw,
                "lx": normalize_axis(raw["lx_raw"]), "ly": normalize_axis(raw["ly_raw"]),
                "rx": normalize_axis(raw["rx_raw"]), "ry": normalize_axis(raw["ry_raw"]),
            }
        return {
            "controller_index": -1, "connected": False, "packet_number": 0,
            "buttons": 0, "left_trigger_raw": 0, "right_trigger_raw": 0,
            "lx_raw": 0, "ly_raw": 0, "rx_raw": 0, "ry_raw": 0,
            "lx": 0.0, "ly": 0.0, "rx": 0.0, "ry": 0.0,
        }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rate-hz", type=float, default=120.0,
                        help="XInput polling rate (30-1000 Hz; default 120).")
    parser.add_argument("--controller-index", type=int, choices=range(4), default=None,
                        help="Pin to XInput user index 0-3; default is first connected controller.")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR,
                        help="Directory for the non-overwriting raw CSV capture.")
    parser.add_argument("--probe", action="store_true",
                        help="Read each controller once and exit without creating files.")
    return parser.parse_args()


def csv_header() -> list[str]:
    return [
        "sample_index", "qpc_ns", "poll_duration_ns", "utc_ns", "controller_index", "connected",
        "packet_number", "button_mask", "left_trigger_raw", "right_trigger_raw",
        "left_x_raw", "left_y_raw", "right_x_raw", "right_y_raw",
        "left_x", "left_y", "right_x", "right_y",
    ]


def state_line(pid: int, session_id: str, qpc_ns: int, poll_duration_ns: int, utc_ns: int,
               sample: dict[str, int | float | bool | None]) -> str:
    values = [
        STATE_MAGIC, str(pid), session_id, str(qpc_ns), str(utc_ns),
        str(sample["controller_index"]), "1" if sample["connected"] else "0",
        str(sample["packet_number"]), str(sample["buttons"]),
        str(sample["left_trigger_raw"]), str(sample["right_trigger_raw"]),
        str(sample["lx_raw"]), str(sample["ly_raw"]),
        str(sample["rx_raw"]), str(sample["ry_raw"]),
        f"{sample['lx']:.7f}", f"{sample['ly']:.7f}",
        f"{sample['rx']:.7f}", f"{sample['ry']:.7f}", str(poll_duration_ns),
    ]
    return "|".join(values) + "\n"


def write_live_state(path: Path, temporary_path: Path, contents: str) -> None:
    with temporary_path.open("w", encoding="ascii", newline="\n") as stream:
        stream.write(contents)
        stream.flush()
        os.fsync(stream.fileno())
    for attempt in range(8):
        try:
            os.replace(temporary_path, path)
            return
        except PermissionError:
            if attempt == 7:
                raise
            time.sleep(0.001)


def acquire_single_instance_lock(path: Path):
    """Hold a Windows byte-range lock; the OS releases it if the process dies."""
    path.parent.mkdir(parents=True, exist_ok=True)
    stream = path.open("a+b")
    try:
        stream.seek(0, os.SEEK_END)
        if stream.tell() == 0:
            stream.write(b"\0")
            stream.flush()
        stream.seek(0)
        try:
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as error:
            raise RuntimeError(
                f"Another XInput recorder already holds the capture lock: {path}"
            ) from error
        return stream
    except Exception:
        stream.close()
        raise


def release_single_instance_lock(stream) -> None:
    try:
        stream.seek(0)
        msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
    finally:
        stream.close()


def run_probe(poller: XInputPoller, requested_index: int | None) -> int:
    if requested_index is not None:
        sample = poller.poll(requested_index)
        print(f"XINPUT_DLL={poller.dll_name} INDEX={requested_index} CONNECTED={sample['connected']} "
              f"LX={sample['lx']:.4f} LY={sample['ly']:.4f} "
              f"RX={sample['rx']:.4f} RY={sample['ry']:.4f}")
        return 0 if sample["connected"] else 2
    connected = []
    for index in range(4):
        sample = poller.poll(index)
        if sample["connected"]:
            connected.append(index)
            print(f"XINPUT_DLL={poller.dll_name} INDEX={index} CONNECTED=1 "
                  f"LX={sample['lx']:.4f} LY={sample['ly']:.4f} "
                  f"RX={sample['rx']:.4f} RY={sample['ry']:.4f}")
    if not connected:
        print(f"XINPUT_DLL={poller.dll_name} CONTROLLER_NONE")
        return 2
    return 0


def main() -> int:
    args = parse_args()
    if not 30.0 <= args.rate_hz <= 1000.0:
        raise ValueError("--rate-hz must be between 30 and 1000.")
    poller = XInputPoller()
    if args.probe:
        return run_probe(poller, args.controller_index)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    DEFAULT_STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
    lock_stream = acquire_single_instance_lock(DEFAULT_LOCK_FILE)
    try:
        if DEFAULT_STATE_FILE.exists():
            raise FileExistsError(
                f"Live-state file already exists; verify no previous recorder is running before removing it: {DEFAULT_STATE_FILE}"
            )

        session_id = uuid.uuid4().hex
        pid = os.getpid()
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        output_path = args.output_dir / f"xinput_{stamp}_{pid}_{session_id[:8]}.csv"
        temporary_path = DEFAULT_STATE_FILE.with_name(
            DEFAULT_STATE_FILE.name + f".{pid}.{session_id}.tmp"
        )
        interval_ns = int(1_000_000_000 / args.rate_hz)
        sample_index = 0

        try:
            with output_path.open("x", encoding="utf-8-sig", newline="") as csv_stream:
                writer = csv.writer(csv_stream)
                writer.writerow(csv_header())
                csv_stream.flush()
                print(f"XINPUT_CAPTURE={output_path}")
                print(f"LIVE_STATE={DEFAULT_STATE_FILE}")
                print(f"RATE_HZ={args.rate_hz:g} CONTROLLER_INDEX={args.controller_index if args.controller_index is not None else 'auto'}")
                print("Press Ctrl+C to stop; this process only reads XInput state.")
                next_deadline_ns = time.perf_counter_ns()
                while True:
                    poll_qpc_before_ns = time.perf_counter_ns()
                    poll_utc_before_ns = time.time_ns()
                    sample = poller.poll(args.controller_index)
                    poll_utc_after_ns = time.time_ns()
                    poll_qpc_after_ns = time.perf_counter_ns()
                    qpc_ns = (poll_qpc_before_ns + poll_qpc_after_ns) // 2
                    poll_duration_ns = poll_qpc_after_ns - poll_qpc_before_ns
                    utc_ns = (poll_utc_before_ns + poll_utc_after_ns) // 2
                    sample_index += 1
                    writer.writerow([
                        sample_index, qpc_ns, poll_duration_ns, utc_ns,
                        sample["controller_index"], 1 if sample["connected"] else 0,
                        sample["packet_number"], sample["buttons"],
                        sample["left_trigger_raw"], sample["right_trigger_raw"],
                        sample["lx_raw"], sample["ly_raw"], sample["rx_raw"], sample["ry_raw"],
                        f"{sample['lx']:.7f}", f"{sample['ly']:.7f}",
                        f"{sample['rx']:.7f}", f"{sample['ry']:.7f}",
                    ])
                    write_live_state(DEFAULT_STATE_FILE, temporary_path,
                                     state_line(pid, session_id, qpc_ns, poll_duration_ns, utc_ns, sample))
                    if sample_index % max(1, round(args.rate_hz)) == 0:
                        csv_stream.flush()
                    next_deadline_ns += interval_ns
                    sleep_ns = next_deadline_ns - time.perf_counter_ns()
                    if sleep_ns > 0:
                        time.sleep(sleep_ns / 1_000_000_000)
                    elif sleep_ns < -interval_ns * 4:
                        next_deadline_ns = time.perf_counter_ns()
        except KeyboardInterrupt:
            print(f"\nXINPUT_STOPPED samples={sample_index} csv={output_path}")
        finally:
            try:
                temporary_path.unlink(missing_ok=True)
            except OSError:
                pass
            try:
                if DEFAULT_STATE_FILE.is_file():
                    fields = DEFAULT_STATE_FILE.read_text(encoding="ascii").strip().split("|")
                    if len(fields) >= 3 and fields[0] == STATE_MAGIC and fields[1] == str(pid) and fields[2] == session_id:
                        DEFAULT_STATE_FILE.unlink()
            except OSError:
                pass
        return 0
    finally:
        release_single_instance_lock(lock_stream)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"XINPUT_CAPTURE_ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
