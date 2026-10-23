"""Regression tests for the messaging system (control server, echo server, clients).

Builds server and client from the sources one directory up into a temporary directory and drives
them over loopback. Needs g++, OpenSSL development headers, the openssl CLI, and free TCP ports
12344 and 12345. Run with:  python3 -m unittest -v messaging-system/tests/test_messaging.py
"""
import os
import re
import shutil
import socket
import subprocess
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(HERE)
CONTROL_PORT = 12344
ECHO_PORT = 12345

TOOLS = shutil.which("g++") and shutil.which("openssl")


@unittest.skipUnless(TOOLS, "needs g++ and openssl")
class MessagingTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bin_dir = tempfile.mkdtemp(prefix="msg-bin-")
        for name in ("server", "client"):
            subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-Wextra", "-O1", os.path.join(SRC, name + ".cpp"),
                 "-o", os.path.join(cls.bin_dir, name), "-lssl", "-lcrypto", "-pthread"],
                check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.bin_dir, ignore_errors=True)

    def setUp(self):
        self.procs = []
        self.work_dir = tempfile.mkdtemp(prefix="msg-run-")

    def tearDown(self):
        for proc in self.procs:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
        shutil.rmtree(self.work_dir, ignore_errors=True)

    def make_cert(self, dns_name):
        subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "server.key",
             "-out", "server.crt", "-days", "2", "-subj", "/CN=" + dns_name,
             "-addext", "subjectAltName=DNS:" + dns_name],
            cwd=self.work_dir, check=True, capture_output=True)

    def start_server(self, port, *args, env=None):
        log = open(os.path.join(self.work_dir, "server-%d.log" % port), "wb")
        full_env = dict(os.environ, **(env or {}))
        proc = subprocess.Popen([os.path.join(self.bin_dir, "server"), str(port), *args],
                                cwd=self.work_dir, stdout=log, stderr=subprocess.STDOUT, env=full_env)
        log.close()  # the child holds its own copy
        self.procs.append(proc)
        deadline = time.time() + 5
        while time.time() < deadline:
            try:
                socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
                return proc
            except OSError:
                time.sleep(0.05)
        self.fail("server on port %d did not start" % port)

    def run_client(self, host, port, stdin, *args, env=None, timeout=8):
        full_env = dict(os.environ, **(env or {}))
        try:
            return subprocess.run([os.path.join(self.bin_dir, "client"), host, str(port), *args],
                                  input=stdin.encode(), capture_output=True, timeout=timeout,
                                  cwd=self.work_dir, env=full_env)
        except subprocess.TimeoutExpired:
            self.fail("client hung (no exit within %ds) for stdin %r" % (timeout, stdin))


class ServerTests(MessagingTestCase):
    def test_server_ignores_sigpipe(self):
        proc = self.start_server(CONTROL_PORT)
        with open("/proc/%d/status" % proc.pid) as status:
            ignored = next(int(line.split()[1], 16) for line in status if line.startswith("SigIgn"))
        self.assertTrue(ignored & (1 << 12), "SIGPIPE is not ignored: SigIgn=%#x" % ignored)


class ControlFramingTests(MessagingTestCase):
    """Control commands are newline-terminated lines, so TCP may split or merge them freely."""

    def read_until(self, sock, needle, count, timeout=3):
        data = b""
        deadline = time.time() + timeout
        sock.settimeout(0.5)
        while data.count(needle) < count and time.time() < deadline:
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                break
            data += chunk
        return data

    def test_two_commands_in_one_segment_are_both_answered(self):
        self.start_server(CONTROL_PORT)
        with socket.create_connection(("127.0.0.1", CONTROL_PORT)) as sock:
            sock.sendall(b"create\ncreate\n")
            data = self.read_until(sock, b"CREATED", 2)
        self.assertEqual(data.count(b"CREATED"), 2, data)
        self.assertNotIn(b"ERROR", data)

    def test_a_command_split_across_segments_is_reassembled(self):
        self.start_server(CONTROL_PORT)
        with socket.create_connection(("127.0.0.1", CONTROL_PORT)) as sock:
            sock.sendall(b"cre")
            time.sleep(0.3)
            sock.sendall(b"ate\n")
            data = self.read_until(sock, b"CREATED", 1)
        self.assertIn(b"CREATED", data)
        self.assertNotIn(b"ERROR", data)

    def test_group_creation_stops_before_the_port_range_overflows(self):
        self.start_server(CONTROL_PORT)
        with socket.create_connection(("127.0.0.1", CONTROL_PORT)) as sock:
            sock.sendall(b"create\n" * 26700)
            data = self.read_until(sock, b"ERROR Too many groups", 1, timeout=20)
        self.assertIn(b"ERROR Too many groups", data)
        ports = [int(p) for p in re.findall(rb"CREATED \d+ (\d+) \d+", data)]
        self.assertTrue(ports and max(ports) <= 65535, max(ports or [0]))

    def test_server_survives_more_clients_than_select_can_watch(self):
        import resource
        soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
        if hard < 2200:
            self.skipTest("cannot raise the file-descriptor limit")
        resource.setrlimit(resource.RLIMIT_NOFILE, (2200, hard))
        self.addCleanup(resource.setrlimit, resource.RLIMIT_NOFILE, (soft, hard))
        self.start_server(CONTROL_PORT)
        socks = [socket.create_connection(("127.0.0.1", CONTROL_PORT)) for _ in range(1100)]
        self.addCleanup(lambda: [x.close() for x in socks])
        time.sleep(1)
        self.assertIsNone(self.procs[0].poll(), "server died")
        for x in socks:
            x.close()
        socks.clear()
        time.sleep(0.5)
        with socket.create_connection(("127.0.0.1", CONTROL_PORT)) as sock:
            sock.sendall(b"create\n")
            self.assertIn(b"CREATED", self.read_until(sock, b"CREATED", 1))

    def test_the_client_still_creates_and_joins_a_group(self):
        self.start_server(CONTROL_PORT)
        result = self.run_client("localhost", 12346, "create\njoin 1\nquit\n")
        self.assertEqual(result.returncode, 0, result.stderr.decode())


class ClientTests(MessagingTestCase):
    def test_non_numeric_group_number_does_not_crash_the_client(self):
        self.start_server(CONTROL_PORT)
        result = self.run_client("localhost", 12346, "join abc\nleave xyz\nquit\n")
        self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_empty_message_does_not_hang_the_echo_client(self):
        self.start_server(ECHO_PORT)
        result = self.run_client("localhost", ECHO_PORT, "\nhello\nquit\n")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertIn(b"TCP echo: hello", result.stdout)

    def test_ipv6_client_reports_an_unknown_multicast_interface(self):
        self.start_server(CONTROL_PORT)
        result = self.run_client("localhost", 12347, "quit\n",
                                 env={"MULTICAST_IFACE": "no-such-iface0"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"no-such-iface0", result.stderr)


class TlsTests(MessagingTestCase):
    def test_tls_echo_works_when_the_certificate_matches_the_hostname(self):
        self.make_cert("localhost")
        self.start_server(ECHO_PORT, "--ssl")
        result = self.run_client("localhost", ECHO_PORT, "hello\nquit\n", "--ssl")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertIn(b"TLS echo: hello", result.stdout)

    def test_tls_client_rejects_a_certificate_for_a_different_hostname(self):
        # The certificate is trusted (it is the pinned trust anchor) but names another host.
        self.make_cert("other.example")
        self.start_server(ECHO_PORT, "--ssl")
        result = self.run_client("localhost", ECHO_PORT, "hello\nquit\n", "--ssl")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn(b"TLS echo", result.stdout)

    def test_silent_connection_does_not_block_the_tls_server_forever(self):
        self.make_cert("localhost")
        self.start_server(ECHO_PORT, "--ssl", env={"ECHO_HANDSHAKE_TIMEOUT_SECS": "1"})
        with socket.create_connection(("127.0.0.1", ECHO_PORT)):
            # This connection never starts a handshake; a real client must still be served.
            result = self.run_client("localhost", ECHO_PORT, "hello\nquit\n", "--ssl", timeout=6)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertIn(b"TLS echo: hello", result.stdout)


if __name__ == "__main__":
    unittest.main()
