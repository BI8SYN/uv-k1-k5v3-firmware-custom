#!/usr/bin/env python3
"""Compare the actual C decoder's output with the recorded fixture payloads."""
import json, pathlib, random, subprocess, sys, tempfile
root=pathlib.Path(__file__).parent
for item in json.loads((root/'fixtures.json').read_text()):
    original=[v if v<128 else v-256 for v in (root/(item['name']+'.s8')).read_bytes()]
    rng=random.Random(1200)
    variants={'original':original,'inverted':[-v for v in original],
              'noise':[v+rng.randrange(-2,3) for v in original]}
    for name,values in variants.items():
        blob=bytes(max(-128,min(127,v))&255 for v in values)
        with tempfile.NamedTemporaryFile() as f:
            f.write(blob); f.flush()
            result=subprocess.run([sys.argv[1],f.name,'verbose'],capture_output=True,text=True)
        assert result.returncode in (0,1) and not result.stderr and result.stdout.splitlines()[-1].startswith("frames="),(result.returncode,result.stdout,result.stderr)
        recovered=[s.split()[1] for s in result.stdout.splitlines() if len(s.split())==2 and s.split()[0].isdigit()]
        assert recovered==[item['expected_payload']] if name!='noise' else set(recovered)<= {item['expected_payload']},(item['name'],name,result.stdout)
        print(item['name'],name,'CRC-valid payloads',len(recovered),'PASS')
