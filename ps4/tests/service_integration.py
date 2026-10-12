#!/usr/bin/env python3
"""Local-only smoke test: phone pages, file range, queue, folder copy and shutdown."""
import os, pathlib, socket, subprocess, tempfile, time, urllib.request, urllib.parse
ROOT = pathlib.Path(__file__).resolve().parents[2]
def command(text):
    with socket.create_connection(('127.0.0.1',9077),timeout=2) as s:
        s.sendall((text+'\n').encode()); s.shutdown(socket.SHUT_WR)
        result=b''
        while chunk:=s.recv(65536): result+=chunk
        return result.decode()
with tempfile.TemporaryDirectory(prefix='pshare-ps4-test-') as temp:
    root=pathlib.Path(temp); data=root/'data'; data.mkdir()
    photo=root/'photo'/'NPXS20001'/'CUSA12345'; photo.mkdir(parents=True)
    video=root/'video'; video.mkdir(); dest=root/'copies'; dest.mkdir()
    capture=photo/'20261012_090000_00000001.jpg'
    # Scanner/streaming tests require the extension, not decoding; avoid external uploads.
    payload=b'PS4 capture binary fixture\x00'*1000; capture.write_bytes(payload)
    (data/'config.ini').write_text(f'''[general]
auto_upload = false
notify = false
default_destination = local
[destination.local]
type = folder
name = Test folder
path = {dest}
''')
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',PSHARE_DATA=str(data),PSHARE_PHOTO=str(root/'photo'),PSHARE_VIDEO=str(video))
    process=subprocess.Popen([str(ROOT/'build/ps4-host/pshare')],cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    try:
        for _ in range(100):
            try:
                if 'pong' in command('PING'): break
            except (OSError,TimeoutError): pass
            time.sleep(.05)
        else: raise AssertionError('service did not start')
        assert 'version=' in command('STATUS app')
        command('SETUP')
        base='http://127.0.0.1:9078'
        for _ in range(100):
            try:
                with urllib.request.urlopen(base,timeout=2) as response: page=response.read()
                break
            except OSError: time.sleep(.05)
        else: raise AssertionError('phone page did not start')
        assert b'Test folder' in page
        with urllib.request.urlopen(base+'/gallery',timeout=2) as response: gallery=response.read()
        assert capture.name.encode() in gallery or b'CUSA12345' in gallery
        with urllib.request.urlopen(base+'/view?p='+urllib.parse.quote(str(capture)),timeout=2) as response:
            assert b'video' in response.read() or response.status==200
        request=urllib.request.Request(base+'/file?p='+urllib.parse.quote(str(capture)),headers={'Range':'bytes=3-17'})
        with urllib.request.urlopen(request,timeout=2) as response:
            assert response.status==206 and response.read()==payload[3:18]
        assert command('QUEUE local\t'+str(capture)).startswith('ok ')
        for _ in range(100):
            copies=list(dest.rglob('*.jpg'))
            if copies: break
            time.sleep(.05)
        assert len(copies)==1 and copies[0].read_bytes()==payload
        assert '\tok\t' in (data/'history.tsv').read_text()
        command('QUIT')
        stopped=False
        for _ in range(100):
            try: command('PING')
            except OSError: stopped=True; break
            time.sleep(.05)
        assert stopped, 'service did not shut down'
        print('Phone setup, gallery, viewer, byte-range, queued folder copy, history and shutdown passed')
    finally:
        if process.poll() is None: process.terminate(); process.wait(timeout=8)
