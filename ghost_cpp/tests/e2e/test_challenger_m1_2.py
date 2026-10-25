"""
Challenger 2 Empirical Verification Suite for Milestone 1 NetworkLogger.
Tests multithreaded concurrency stress, timestamp monotonicity, dual-file mirroring,
and credential sanitization.
"""

import unittest
import os
import subprocess
import re

TESTS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BINARY_PATH = os.path.join(TESTS_DIR, "challenge_network_logger.exe")


class TestNetworkLoggerEmpiricalChallenge(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Verify the challenge binary exists; if not, compile it
        if not os.path.exists(BINARY_PATH):
            build_cmd = (
                'call "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64 && '
                f'cd /d "{TESTS_DIR}\\.." && '
                'cl /EHsc /std:c++17 /MT /O2 /I"include" '
                '/I"C:\\Users\\nodir\\Desktop\\project 2\\SDK_TEST\\WINDOWS KITS\\10\\INCLUDE\\10.0.26100.0\\UCRT" '
                '/I"C:\\Users\\nodir\\Desktop\\project 2\\SDK_TEST\\WINDOWS KITS\\10\\INCLUDE\\10.0.26100.0\\UM" '
                '/I"C:\\Users\\nodir\\Desktop\\project 2\\SDK_TEST\\WINDOWS KITS\\10\\INCLUDE\\10.0.26100.0\\SHARED" '
                'tests\\challenge_network_logger.cpp src\\network_logger.cpp '
                '/Fe:tests\\challenge_network_logger.exe /link '
                '/LIBPATH:"C:\\Users\\nodir\\Desktop\\project 2\\SDK_TEST\\WINDOWS KITS\\10\\LIB\\10.0.26100.0\\UM\\X64" '
                '/LIBPATH:"C:\\Users\\nodir\\Desktop\\project 2\\SDK_TEST\\WINDOWS KITS\\10\\LIB\\10.0.26100.0\\UCRT\\X64" '
                'winhttp.lib advapi32.lib user32.lib'
            )
            res = subprocess.run(f'cmd.exe /c "{build_cmd}"', shell=True, capture_output=True, text=True)
            if res.returncode != 0:
                raise RuntimeError(f"Failed to compile challenge_network_logger.exe:\n{res.stderr}\n{res.stdout}")

    def test_01_binary_execution_and_findings(self):
        """Execute empirical native binary and extract empirical metrics."""
        proc = subprocess.run([BINARY_PATH], capture_output=True, text=True)
        output = proc.stdout

        # Verify dual file mirroring passed
        self.assertIn("[PASS] Dual-file mirroring: [NETWORK_FAILOVER] & [NETWORK_FAILURE]", output)
        self.assertIn("[PASS] Routine logs isolated from ghost_startup.log", output)

        # Verify zero dropped lines under 16-thread stress
        self.assertIn("[PASS] Concurrency Stress: Zero dropped lines in network.log", output)
        self.assertIn("[PASS] Concurrency Stress: Exact mirroring count in ghost_startup.log", output)
        self.assertIn("[PASS] Concurrency Stress: Line integrity (zero torn or corrupted lines)", output)

        # Check for Timestamp Monotonicity check passing
        self.assertIn("[PASS] Timestamp Monotonicity check under concurrency", output,
                      "Timestamp monotonicity flaw should be resolved")

        # Check for Schemeless URL password masked
        self.assertIn("[PASS] Adversarial: Schemeless proxy URL (admin:pass@host:port)", output,
                      "Schemeless URL password should be masked")

        self.assertEqual(proc.returncode, 0, f"Binary failed with return code {proc.returncode}:\n{output}")


if __name__ == "__main__":
    unittest.main()
