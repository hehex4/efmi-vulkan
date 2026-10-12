"""Copy the sample's unchanged component-7 BUF bytes into an explicit M2 test package.

The installation is read-only. This is one fixed component, not interpretation of the mod's INI.
The package is uncommitted and records asset fingerprints in a sibling manifest.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"server"))
from efmi_vk.hashes import crc32c


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mod",type=Path,required=True)
    ap.add_argument("--out",type=Path,default=ROOT/"build/m2-component7.bin")
    args=ap.parse_args()
    out=args.out.resolve()
    if not out.is_relative_to(ROOT):
        raise ValueError("package output must stay in this project")
    ini=args.mod/"arcane.ini"
    ini_hash=hashlib.sha256(ini.read_bytes()).hexdigest()
    if ini_hash != "697d0c98378b83d3aef592a54536470bff4410993f475c1514ed701c4b6f71bd":
        raise ValueError("sample INI differs from the reviewed component-7 profile; re-evaluate before packaging")
    blocks=[];files=[]
    for stream,size in (("Position",640),("Texcoord",320),("Blend",640),("Index",432)):
        source=args.mod/"Meshes"/f"a3fe5c47-108-0-{stream}.buf"
        data=source.read_bytes()
        if len(data)!=size:
            raise ValueError(f"sample component changed: {source}")
        blocks.append(data)
        files.append({"path":str(source.resolve()),"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()})
    payload=b"".join(blocks)
    header=struct.pack("<8s12I",b"EFMIM201",0xa3fe5c47,108,0,499,266,12,40,108,16,8,16,crc32c(payload))
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_bytes(header+payload)
    report={"scope":"M2 component 7 only; original BUF bytes; no texture overrides or INI engine",
            "ini":{"path":str(ini.resolve()),"sha256":ini_hash},
            "files":files,"package_sha256":hashlib.sha256(out.read_bytes()).hexdigest()}
    out.with_suffix(".json").write_text(json.dumps(report,indent=2)+"\n")
    print(out)


if __name__=="__main__":
    main()
