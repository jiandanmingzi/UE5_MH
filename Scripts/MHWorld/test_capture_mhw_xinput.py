import ctypes
import importlib.util
from pathlib import Path
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parent / "capture_mhw_xinput.py"
SPEC = importlib.util.spec_from_file_location("capture_mhw_xinput", SCRIPT)
CAPTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CAPTURE)


class XInputCaptureTests(unittest.TestCase):
    def test_axis_normalization_preserves_endpoints_without_deadzone(self):
        self.assertEqual(CAPTURE.normalize_axis(-32768), -1.0)
        self.assertEqual(CAPTURE.normalize_axis(0), 0.0)
        self.assertEqual(CAPTURE.normalize_axis(32767), 1.0)
        self.assertAlmostEqual(CAPTURE.normalize_axis(1), 1.0 / 32767.0)

    def test_state_line_has_fixed_pipe_schema_and_high_precision_timestamps(self):
        sample = {
            "controller_index": 0, "connected": True, "packet_number": 8,
            "buttons": 4096, "left_trigger_raw": 0, "right_trigger_raw": 255,
            "lx_raw": -32768, "ly_raw": 16384, "rx_raw": 0, "ry_raw": 32767,
            "lx": -1.0, "ly": CAPTURE.normalize_axis(16384),
            "rx": 0.0, "ry": 1.0,
        }
        line = CAPTURE.state_line(
            12, "abc123", 1234567890123456789, 321, 987654321012345678, sample
        )
        fields = line.rstrip("\n").split("|")
        self.assertEqual(len(fields), 20)
        self.assertEqual(fields[0], CAPTURE.STATE_MAGIC)
        self.assertEqual(fields[3], "1234567890123456789")
        self.assertEqual(fields[4], "987654321012345678")
        self.assertEqual(fields[15], "-1.0000000")
        self.assertEqual(fields[18], "1.0000000")
        self.assertEqual(fields[19], "321")

    def test_poller_decodes_connected_controller(self):
        poller = CAPTURE.XInputPoller.__new__(CAPTURE.XInputPoller)

        def fake_get_state(index, state_ptr):
            state = ctypes.cast(state_ptr, ctypes.POINTER(CAPTURE.XInputState)).contents
            state.dwPacketNumber = 23
            state.Gamepad.wButtons = 0x1000
            state.Gamepad.bLeftTrigger = 14
            state.Gamepad.bRightTrigger = 210
            state.Gamepad.sThumbLX = -32768
            state.Gamepad.sThumbLY = 16384
            state.Gamepad.sThumbRX = 0
            state.Gamepad.sThumbRY = 32767
            return CAPTURE.ERROR_SUCCESS

        poller.get_state = fake_get_state
        sample = poller.poll(2)
        self.assertTrue(sample["connected"])
        self.assertEqual(sample["controller_index"], 2)
        self.assertEqual(sample["lx_raw"], -32768)
        self.assertEqual(sample["lx"], -1.0)
        self.assertEqual(sample["ry"], 1.0)
        self.assertEqual(sample["buttons"], 0x1000)

    def test_poller_reports_disconnected_without_fabricating_axes(self):
        poller = CAPTURE.XInputPoller.__new__(CAPTURE.XInputPoller)
        poller.get_state = lambda index, state_ptr: 1167
        sample = poller.poll(None)
        self.assertFalse(sample["connected"])
        self.assertEqual(sample["controller_index"], -1)
        self.assertEqual((sample["lx_raw"], sample["ly_raw"]), (0, 0))

    def test_live_state_is_written_atomically(self):
        class MemoryPath:
            def __init__(self):
                self.contents = ""

            def open(self, *_args, **_kwargs):
                path = self

                class MemoryStream:
                    def __enter__(self):
                        return self

                    def __exit__(self, *_args):
                        return False

                    def write(self, contents):
                        path.contents = contents

                    def flush(self):
                        pass

                    def fileno(self):
                        return 1

                return MemoryStream()

        temporary, destination = MemoryPath(), MemoryPath()

        def fake_replace(source, target):
            target.contents = source.contents

        with mock.patch.object(CAPTURE.os, "fsync"), \
                mock.patch.object(CAPTURE.os, "replace", side_effect=fake_replace):
            CAPTURE.write_live_state(destination, temporary, "test-state\n")
        self.assertEqual(destination.contents, "test-state\n")

    def test_single_instance_lock_rejects_second_owner_and_releases(self):
        class MemoryLockStream:
            def __init__(self):
                self.position = 0
                self.contents = bytearray()
                self.closed = False

            def seek(self, offset, whence=0):
                self.position = len(self.contents) if whence == 2 else offset

            def tell(self):
                return len(self.contents)

            def write(self, value):
                self.contents.extend(value)

            def flush(self):
                pass

            def fileno(self):
                return 1

            def close(self):
                self.closed = True

        class MemoryLockPath:
            parent = None

            def __init__(self):
                self.parent = self

            def mkdir(self, **_kwargs):
                pass

            def open(self, _mode):
                return MemoryLockStream()

        lock_held = False

        def fake_lock(_fd, mode, _count):
            nonlocal lock_held
            if mode == CAPTURE.msvcrt.LK_NBLCK:
                if lock_held:
                    raise OSError("lock is held")
                lock_held = True
            else:
                lock_held = False

        with mock.patch.object(CAPTURE.msvcrt, "locking", side_effect=fake_lock):
            first = CAPTURE.acquire_single_instance_lock(MemoryLockPath())
            with self.assertRaises(RuntimeError):
                CAPTURE.acquire_single_instance_lock(MemoryLockPath())
            CAPTURE.release_single_instance_lock(first)
            second = CAPTURE.acquire_single_instance_lock(MemoryLockPath())
            CAPTURE.release_single_instance_lock(second)
        self.assertFalse(lock_held)


if __name__ == "__main__":
    unittest.main()
