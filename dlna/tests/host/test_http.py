#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the real HTTP server, or a board (--device IP --local-ip IP), over TCP."""
import argparse
import http.server
import queue
import socket
import subprocess
import threading
import time
import xml.etree.ElementTree as ET
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--server')
p.add_argument('--device', default='127.0.0.1')
p.add_argument('--port', type=int, default=49152)
p.add_argument('--local-ip', default='127.0.0.1')
p.add_argument('--interval', type=float, default=0, help='Seconds between requests on a board')
a = p.parse_args()
events = queue.Queue()


class Callback(http.server.BaseHTTPRequestHandler):
    def do_NOTIFY(self):
        body = self.rfile.read(int(self.headers['Content-Length']))
        events.put((self.headers['SID'], int(self.headers['SEQ']), ET.fromstring(body)))
        self.send_response(200)
        self.send_header('Content-Length', '0')
        self.end_headers()

    def log_message(self, *args):
        pass


callback = http.server.ThreadingHTTPServer(('0.0.0.0', 0), Callback)
threading.Thread(target=callback.serve_forever, daemon=True).start()
proc = None
log = None
if a.server:
    log = open(Path(a.server).with_suffix('.log'), 'w')
    proc = subprocess.Popen([a.server], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)
    a.port = int(proc.stdout.readline())


def exchange(method, path, headers=None, body=b'', expect=False):
    if a.interval:
        time.sleep(a.interval)
    headers = dict(headers or {})
    if body or method == 'POST':
        headers.setdefault('Content-Length', str(len(body)))
    if expect:
        headers['Expect'] = '100-continue'
    head = f'{method} {path} HTTP/1.1\r\nHost: {a.device}\r\n'
    head += ''.join(f'{k}: {v}\r\n' for k, v in headers.items()) + '\r\n'
    with socket.create_connection((a.device, a.port), timeout=4) as s:
        s.sendall(head.encode() + (b'' if expect else body))
        if expect:
            interim = b''
            while not interim.endswith(b'\r\n\r\n'):
                interim += s.recv(1)
            assert interim == b'HTTP/1.1 100 Continue\r\n\r\n', interim
            s.sendall(body)
        # Read through EOF: Content-Length alone would conceal subscription socket leaks.
        response = b''
        while chunk := s.recv(65536):
            response += chunk
    h, data = response.split(b'\r\n\r\n', 1)
    lines = h.decode().split('\r\n')
    fields = dict(line.lower().split(': ', 1) for line in lines[1:] if ': ' in line)
    if method != 'HEAD':
        assert len(data) == int(fields['content-length'])
    return int(lines[0].split()[1]), fields, data


def soap(service, action, args='', expect=False):
    body = (f'<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body>'
            f'<u:{action} xmlns:u="urn:schemas-upnp-org:service:{service}:1">'
            f'<InstanceID>0</InstanceID>{args}</u:{action}></s:Body></s:Envelope>').encode()
    return exchange('POST', f'/{service}/control', {
        'SOAPACTION': f'"urn:schemas-upnp-org:service:{service}:1#{action}"',
        'Content-Type': 'text/xml'}, body, expect)


def event(sid, check):
    deadline = time.monotonic() + 4
    while time.monotonic() < deadline:
        got_sid, seq, root = events.get(timeout=max(0.01, deadline - time.monotonic()))
        if got_sid == sid and check(root):
            return seq, root
    raise AssertionError('Missing event')


def change(root):
    return ET.fromstring(root.find('.//LastChange').text)


try:
    assert exchange('GET', '/description.xml')[0] == 200
    for svc in ['AVTransport', 'RenderingControl', 'ConnectionManager']:
        assert exchange('GET', f'/{svc}/scpd.xml')[0] == 200
        # More cycles than the board's socket table capacity, including renewals and failures.
        for i in range(12):
            code, fields, _ = exchange('SUBSCRIBE', f'/{svc}/event', {
                'CALLBACK': f'<http://{a.local_ip}:{callback.server_port}/events>', 'NT': 'upnp:event'})
            assert code == 200
            sid = fields['sid']
            seq, root = event(sid, lambda r: True)
            assert seq == 0
            if svc == 'ConnectionManager':
                assert 'video/mp4' in root.find('.//SinkProtocolInfo').text
                assert root.find('.//CurrentConnectionIDs').text == '0'
            else:
                assert change(root).find('.//{*}InstanceID').attrib['val'] == '0'
            assert exchange('SUBSCRIBE', f'/{svc}/event', {'SID': sid})[0] == 200
            if i == 0:
                other = 'RenderingControl' if svc != 'RenderingControl' else 'AVTransport'
                assert exchange('SUBSCRIBE', f'/{other}/event', {'SID': sid})[0] == 412
            assert exchange('UNSUBSCRIBE', f'/{svc}/event', {'SID': sid})[0] == 200
        assert exchange('SUBSCRIBE', f'/{svc}/event', {'SID': 'uuid:missing'})[0] == 412
        assert exchange('SUBSCRIBE', f'/{svc}/event', {'CALLBACK': '<http://bad'})[0] == 412
        assert exchange('SUBSCRIBE', f'/{svc}/event')[0] == 412
    # RenderingControl notices after SOAP mutations, not only the initial event.
    code, fields, _ = exchange('SUBSCRIBE', '/RenderingControl/event', {
        'CALLBACK': f'<http://{a.local_ip}:{callback.server_port}/events>', 'NT': 'upnp:event'})
    assert code == 200
    sid = fields['sid']
    event(sid, lambda r: True)
    assert soap('RenderingControl', 'SetVolume', '<Channel>Master</Channel><DesiredVolume>37</DesiredVolume>')[0] == 200
    seq, _ = event(sid, lambda r: change(r).find('.//{*}Volume').attrib['val'] == '37')
    assert seq > 0
    assert soap('RenderingControl', 'SetMute', '<Channel>Master</Channel><DesiredMute>1</DesiredMute>')[0] == 200
    event(sid, lambda r: change(r).find('.//{*}Mute').attrib['val'] == '1')
    assert soap('RenderingControl', 'SetMute', '<Channel>Master</Channel><DesiredMute>0</DesiredMute>')[0] == 200
    assert soap('RenderingControl', 'SetVolume', '<Channel>Master</Channel><DesiredVolume>50</DesiredVolume>')[0] == 200
    assert exchange('UNSUBSCRIBE', '/RenderingControl/event', {'SID': sid})[0] == 200
    # Controllers can wait for Continue before sending their SOAP body.
    code, _, body = soap('AVTransport', 'GetTransportInfo', expect=True)
    assert code == 200 and b'GetTransportInfoResponse' in body
    assert exchange('POST', '/AVTransport/control', {'Content-Length': '-1'})[0] == 400
    assert exchange('GET', '/description.xml')[0] == 200
    assert exchange('HEAD', '/description.xml')[0] == 200
    if a.server:
        # Signed addresses exceed the old 512-byte limit, then pass through both event XML layers.
        uri = 'http://127.0.0.1/video.mp4?token=' + 'x' * 3000 + '&part=1'
        assert soap('AVTransport', 'SetAVTransportURI',
                    '<CurrentURI>' + uri.replace('&', '&amp;') + '</CurrentURI>')[0] == 200
        code, _, body = soap('AVTransport', 'GetMediaInfo')
        assert code == 200 and ET.fromstring(body).find('.//CurrentURI').text == uri
        code, fields, _ = exchange('SUBSCRIBE', '/AVTransport/event', {
            'CALLBACK': f'<http://{a.local_ip}:{callback.server_port}/events>', 'NT': 'upnp:event'})
        assert code == 200
        sid = fields['sid']
        _, root = event(sid, lambda r: True)
        assert change(root).find('.//{*}AVTransportURI').attrib['val'] == uri
        assert exchange('UNSUBSCRIBE', '/AVTransport/event', {'SID': sid})[0] == 200
        assert soap('AVTransport', 'SetAVTransportURI', '<CurrentURI>' + 'x' * 4096 + '</CurrentURI>')[0] == 500
        assert soap('AVTransport', 'Play', '<Speed>1</Speed>')[0] == 500
        code, _, body = soap('AVTransport', 'GetTransportInfo')
        assert code == 200 and b'ERROR_OCCURRED' in body
        assert soap('AVTransport', 'SetAVTransportURI', '<CurrentURI/>')[0] == 200
    print('PASS: HTTP descriptions, subscriptions, callbacks, renewal, socket cleanup, volume events, Expect')
finally:
    if proc:
        proc.communicate(b'\n', timeout=8)
        assert proc.returncode == 0
    if log:
        log.close()
    callback.shutdown()
