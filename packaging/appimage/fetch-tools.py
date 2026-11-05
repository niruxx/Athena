#!/usr/bin/env python3
"""Download pinned HTTPS build tools, verifying every byte before execution."""
import hashlib,json,pathlib,sys,urllib.request
config=json.loads((pathlib.Path(__file__).parent/'tools.json').read_text())
destination=pathlib.Path(sys.argv[1]);destination.mkdir(parents=True,exist_ok=True)
for key,name in [('linuxdeploy','linuxdeploy-x86_64.AppImage'),('gtk_plugin','linuxdeploy-plugin-gtk.sh')]:
    item=config[key];target=destination/name
    if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest()!=item['sha256']:
        with urllib.request.urlopen(item['url'],timeout=120) as response: data=response.read()
        if hashlib.sha256(data).hexdigest()!=item['sha256']:raise SystemExit(f'{name}: checksum mismatch; review the upstream pin before updating tools.json')
        target.write_bytes(data)
    target.chmod(0o755)
