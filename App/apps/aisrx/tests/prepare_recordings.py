#!/usr/bin/env python3
"""Regenerate test windows from downloaded originals, without audio playback."""
import hashlib,json,pathlib,struct,sys,wave
source=pathlib.Path(sys.argv[1]);root=pathlib.Path(__file__).parent
with wave.open(str(source/'long-beach-160-messages.wav'),'rb') as w:
    assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(1,1,48000)
    long_beach=bytes(v^128 for v in w.readframes(w.getnframes()))
raw=(source/'helsinki-210-messages.raw').read_bytes()
helsinki=bytes((struct.unpack_from('<h',raw,i)[0]>>8)&255 for i in range(0,len(raw),4))
for f in json.loads((root/'fixtures.json').read_text()):
    samples=long_beach if f['name']=='long-beach' else helsinki
    start=f['source_sample_offset'];window=samples[start:start+f['length']]
    assert hashlib.sha256(window).hexdigest()==f['sha256'],f['name']
    (root/(f['name']+'.s8')).write_bytes(window)
    print(f['name'],'fixture hash PASS')
