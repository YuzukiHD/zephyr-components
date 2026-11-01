# SPDX-License-Identifier: Apache-2.0
"""Independent RTSP source driving the actual socket receiver over loopback."""
from contextlib import ExitStack
import pathlib
import select
import socket
import struct
import subprocess
import sys
import time


class RTSP:
    def __init__(self, connection):
        self.connection = connection
        self.buffer = b""
        self.headers = {}
        self.keepalives = 0

    def send(self, first, seq, body="", extra=""):
        msg = (f"{first}\r\nCSeq: {seq}\r\n{extra}Content-Length: {len(body)}\r\n\r\n{body}").encode()
        # Deliberately split both the headers and body across TCP writes.
        for pos in range(0, len(msg), 13):
            self.connection.sendall(msg[pos:pos + 13])

    def read(self):
        while True:
            if b"\r\n\r\n" in self.buffer:
                header, body = self.buffer.split(b"\r\n\r\n", 1)
                lines = header.decode().split("\r\n")
                fields = dict(line.split(": ", 1) for line in lines[1:])
                length = int(fields.get("Content-Length", 0))
                if len(body) >= length:
                    self.buffer = body[length:]
                    self.headers = fields
                    return lines[0], int(fields["CSeq"]), body[:length].decode()
            data = self.connection.recv(8192)
            assert data, "Unexpected TCP EOF"
            self.buffer += data

    def answer(self, message):
        first, cseq, body = message
        assert self.headers.get("Session") == "source-session"
        if first.startswith("GET_PARAMETER "):
            assert not body
            self.keepalives += 1
        else:
            assert first.startswith("SET_PARAMETER ") and "wfd_idr_request" in body
        self.send("RTSP/1.0 200 OK", cseq, extra="Session: source-session\r\n")


def run(executable, ts_file, bye):
    with ExitStack() as sockets:
        listener = sockets.enter_context(socket.socket())
        rtp = sockets.enter_context(socket.socket(type=socket.SOCK_DGRAM))
        rtcp = sockets.enter_context(socket.socket(type=socket.SOCK_DGRAM))
        listener.bind(("127.0.0.1", 0))
        listener.listen(32)
        listener.settimeout(5)
        # A source may explicitly negotiate a nonadjacent RTCP port.
        while True:
            rtp.bind(("127.0.0.1", 0))
            port = rtp.getsockname()[1]
            if port % 2 == 0:
                try:
                    rtcp.bind(("127.0.0.1", 0))
                    if rtcp.getsockname()[1] != port + 1:
                        break
                    rtcp.close()
                    rtcp = sockets.enter_context(socket.socket(type=socket.SOCK_DGRAM))
                except OSError:
                    pass
            rtp.close()
            rtp = sockets.enter_context(socket.socket(type=socket.SOCK_DGRAM))
        sink_port = 19000
        proc = subprocess.Popen([executable, str(listener.getsockname()[1]), str(sink_port)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            connection, _ = listener.accept()
            with connection:
                connection.settimeout(3)
                source = RTSP(connection)
                source.send("OPTIONS * RTSP/1.0", 10)
                assert source.read()[:2] == ("RTSP/1.0 200 OK", 10)
                assert source.read()[0] == "OPTIONS * RTSP/1.0"
                source.send("RTSP/1.0 200 OK", 1)
                source.send("GET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0", 11,
                            "wfd_video_formats\r\nwfd_audio_codecs\r\nwfd_client_rtp_ports\r\n")
                assert "LPCM" in source.read()[2]
                source.send("SET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0", 12,
                            "wfd_video_formats: 00 00 01 01 00000001 00000000 00000000 00 0000 0000 00 none none\r\n"
                            "wfd_audio_codecs: AAC 00000001 00\r\n"
                            "wfd_presentation_URL: rtsp://127.0.0.1/wfd1.0/streamid=0 none\r\n")
                assert source.read()[:2] == ("RTSP/1.0 200 OK", 12)
                source.send("SET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0", 13,
                            "wfd_trigger_method: SETUP\r\n")
                assert source.read()[:2] == ("RTSP/1.0 200 OK", 13)
                assert source.read()[0].startswith("SETUP ")
                source.send("RTSP/1.0 200 OK", 2, extra=
                            f"Session: source-session;timeout=2\r\nTransport: RTP/AVP;unicast;client_port={sink_port}-{sink_port + 1};server_port={port}-{rtcp.getsockname()[1]}\r\n")
                assert source.read()[0].startswith("PLAY ")
                source.send("RTSP/1.0 200 OK", 3)
                ts = pathlib.Path(ts_file).read_bytes()
                # A datagram from an unnegotiated source port must not change the SSRC.
                rtcp.sendto(struct.pack("!BBHII", 0x80, 33, 1234, 0, 0xDEADBEEF) + ts[:188],
                            ("127.0.0.1", sink_port))
                for seq, pos in enumerate(range(0, len(ts), 188 * 7)):
                    header = struct.pack("!BBHII", 0x80, 33, seq, seq * 300, 0x01020304)
                    rtp.sendto(header + ts[pos:pos + 188 * 7], ("127.0.0.1", sink_port))
                    while source.buffer or select.select([connection], [], [], 0)[0]:
                        source.answer(source.read())
                    time.sleep(0.002)

                def receiver_report(last_sr):
                    deadline = time.monotonic() + 3
                    while time.monotonic() < deadline:
                        readable = select.select([rtcp, connection], [], [], 0.1)[0]
                        if source.buffer or connection in readable:
                            source.answer(source.read())
                        if rtcp in readable:
                            report, _ = rtcp.recvfrom(1024)
                            assert report[1] == 201 and report[33] == 202
                            assert report[42:50] == b"miracast"
                            assert struct.unpack_from("!I", report, 8)[0] == 0x01020304
                            if struct.unpack_from("!I", report, 24)[0] == last_sr:
                                assert report[12:16] == b"\0\0\0\0", "Unexpected RTP loss"
                                return
                    raise AssertionError("No RTCP report at the negotiated port")

                # The first report must reach the negotiated port before any SR arrives.
                receiver_report(0)
                # Send an NTP sender report and verify the receiver's report/SDES.
                sr = struct.pack("!BBHIIIIII", 0x80, 200, 6, 0x01020304,
                                 0x10203040, 0x50607080, 123456, seq + 1, len(ts))
                rtcp.sendto(sr, ("127.0.0.1", sink_port + 1))
                receiver_report(0x30405060)
                deadline = time.monotonic() + 3
                while source.keepalives < 2 and time.monotonic() < deadline:
                    source.answer(source.read())
                assert source.keepalives >= 2, "Session timeout did not drive keepalives"
                # Finish any IDR exchange before asking the sink to close TCP.
                source.send("GET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0", 99,
                            extra="Session: source-session\r\n")
                while True:
                    first, cseq, body = source.read()
                    if first.startswith("RTSP/1.0"):
                        assert cseq == 99
                        assert source.headers.get("Session") == "source-session"
                        break
                    source.answer((first, cseq, body))
                if bye:
                    # Truncated BYE and another SSRC's BYE must not end the session.
                    rtcp.sendto(struct.pack("!BBH", 0x81, 203, 1), ("127.0.0.1", sink_port + 1))
                    rtcp.sendto(struct.pack("!BBHI", 0x81, 203, 1, 0xDEADBEEF),
                                ("127.0.0.1", sink_port + 1))
                    source.send("GET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0", 100)
                    while True:
                        message = source.read()
                        if message[0].startswith("RTSP/1.0"):
                            assert message[:2] == ("RTSP/1.0 200 OK", 100)
                            break
                        source.answer(message)
                    rtcp.sendto(struct.pack("!BBHI", 0x81, 203, 1, 0x01020304),
                                ("127.0.0.1", sink_port + 1))
                    while connection.recv(1024):
                        pass
                else:
                    source.send("TEARDOWN rtsp://localhost/wfd1.0 RTSP/1.0", 14)
                    assert source.read()[:2] == ("RTSP/1.0 200 OK", 14)
            stdout, stderr = proc.communicate(timeout=10)
            assert proc.returncode == 0, stderr + stdout
            print(("RTCP BYE" if bye else "RTSP TEARDOWN") + ": " + stdout.strip())
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()


if __name__ == "__main__":
    for bye in (False, True):
        run(*sys.argv[1:], bye)
