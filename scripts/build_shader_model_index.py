"""Prove source shader models from paired API programs, without inferring DXBC hashes.

Each exact SPIR-V identity must have one model across all observed counterparts.
The runtime cache contains no asset, shader or character names and no GPU assets.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import struct
import sys

from unity_shader_metadata import Smolv, program, require, shader_tree, span, u32

ROOT = Path(__file__).resolve().parents[1]
MAGIC = b'EFVSM001'
MAX_BYTES = 8 * 1024 * 1024


def model_name(token):
    kind, major, minor = token >> 16, (token >> 4) & 15, token & 15
    require(kind < 6 and major in (4, 5) and minor <= 1 and token & 0xff00 == 0,
            'Unsupported DXBC shader model')
    return 'pvghdc'[kind] + 's_' + str(major) + '_' + str(minor)


def dxbc_model(data):
    require(len(data) >= 32 and data[:4] == b'DXBC' and u32(data,24) == len(data), 'Invalid DXBC container')
    count = u32(data,28)
    require(0 < count <= (len(data)-32)//4, 'Invalid DXBC chunk table')
    intervals, models = [], []
    for index in range(count):
        offset = u32(data,32+4*index)
        require(offset >= 32+4*count, 'DXBC chunk overlaps its table')
        size = u32(span(data,offset,8),4)
        chunk = span(data,offset+8,size)
        intervals.append((offset,offset+8+size))
        if data[offset:offset+4] in (b'SHDR',b'SHEX'):
            require(len(chunk)>=8 and 2 <= u32(chunk,4) <= len(chunk)//4, 'Invalid DXBC shader payload')
            models.append(u32(chunk))
    intervals.sort()
    require(all(a[1]<=b[0] for a,b in zip(intervals,intervals[1:])), 'Overlapping DXBC chunks')
    require(len(models)==1, 'Expected exactly one DXBC shader program')
    model_name(models[0])
    return models[0]


def spirv_stage(data):
    require(len(data)>=20 and len(data)%4==0 and u32(data)==0x07230203, 'Invalid SPIR-V header')
    words = struct.unpack('<%dI' % (len(data)//4),data)
    pos, stages = 5, []
    while pos<len(words):
        length, opcode = words[pos]>>16, words[pos]&65535
        require(length>0 and pos+length<=len(words), 'Invalid SPIR-V instruction')
        if opcode==15:
            require(length>=4 and words[pos+1]<6, 'Unsupported SPIR-V entry point')
            stages.append('vhdgpc'[words[pos+1]])
        pos+=length
    require(len(stages)==1, 'Expected exactly one SPIR-V entry point')
    return stages[0]


def payloads(code):
    # Observed combined-program header: six offset/length pairs. Shader stages
    # come from actual executable headers, never from the serialized slot order.
    require(len(code)>=176 and u32(code)>>24==2,
            'Unknown combined-program header: '+code[:16].hex())
    require(not any(code[52:76]), 'Unsupported extra program payloads')
    ranges = []
    for index in range(6):
        offset,size = struct.unpack('<2I',span(code,4+8*index,8))
        require(bool(offset)==bool(size), 'Incomplete combined-program range')
        if size:
            require(offset>=176, 'Program payload overlaps metadata')
            span(code,offset,size)
            ranges.append((offset,offset+size))
    ranges.sort()
    require(ranges and ranges[0][0]==176 and ranges[-1][1]==len(code) and
            all(a[1]==b[0] for a,b in zip(ranges,ranges[1:])), 'Incomplete or overlapping combined-program payloads')
    return [code[a:b] for a,b in ranges]


def encode(records):
    require(len(records)<=65536, 'Too many shader model entries')
    payload = bytearray()
    seen = set()
    for digest,size,token in sorted(records):
        require(len(digest)==32 and digest not in seen and 20<=size<=16*1024*1024 and size%4==0,
                'Invalid or duplicate shader model identity')
        model_name(token)
        seen.add(digest)
        payload += digest + struct.pack('<2I',size,token)
    require(len(payload)<=MAX_BYTES-48, 'Shader model cache budget exceeded')
    return MAGIC+struct.pack('<2I',len(records),len(payload))+hashlib.sha256(payload).digest()+payload


def build(args):
    sys.path.insert(0,str(args.packages.resolve()))
    import UnityPy
    import lz4.block
    require(UnityPy.__version__=='1.25.2', 'Use the audited UnityPy schema')
    output = args.output.resolve()
    require(output.is_relative_to(ROOT/'build'), 'Generated cache must stay inside this checkout build directory')
    output.mkdir(parents=True,exist_ok=True)
    decoder = Smolv(args.decoder.resolve())
    sources = json.loads(args.sources.read_text())['sources']
    models, sizes, unpaired, stats = defaultdict(set), {}, set(), Counter()
    representatives = set()
    for source in sources:
        path = Path(source['path'])
        fingerprint = lambda: hashlib.sha256(path.read_bytes()).hexdigest()
        require(fingerprint()==source['sha256'], 'Source evidence changed')
        for obj in UnityPy.load(str(path)).objects:
            if obj.type.name!='Shader': continue
            embedded = obj.serialized_type.node if obj.serialized_type else None
            data = obj.read_typetree(nodes=embedded if embedded is not None else shader_tree(obj._get_typetree_node()),check_read=True)
            require(data['platforms']==[4,18] and data['m_CompressionType']==3 and
                    data['m_EnableShaderLODStreaming'] and not data['compressedBlob'], 'Unsupported program schema')
            subs,chunks = data['m_ParsedForm']['m_SubShaders'],data['subShaderBlobs']
            require(len(subs)==len(chunks), 'Subshader payload count differs')
            for sub,chunk in zip(subs,chunks):
                require(sub['m_LOD']==chunk['m_ShaderLOD'], 'Subshader LOD differs')
                platforms=[]
                for api in range(2):
                    offsets,lengths,decoded = [chunk[key][api] for key in ('m_Offsets','m_CompressedLengths','m_DecompressedLengths')]
                    require(len(offsets)==len(lengths)==len(decoded) and decoded and
                            all(0<n<=64*1024*1024 for n in decoded), 'Invalid compressed program dimensions')
                    segments = [lz4.block.decompress(span(bytes(chunk['m_CompressedBlob']),off,size),uncompressed_size=n)
                                for off,size,n in zip(offsets,lengths,decoded)]
                    require([len(x) for x in segments]==decoded, 'Decoded program lengths differ')
                    platforms.append(segments)
                cached = {}
                def stages(api,index):
                    key=(api,index)
                    if key not in cached:
                        code,_ = program(platforms[api],index,(33,25)[api])
                        result={}
                        for payload in payloads(code):
                            if api:
                                module=decoder.decode(payload);stage=spirv_stage(module)
                                value=(hashlib.sha256(module).hexdigest(),len(module))
                                if stage not in representatives:
                                    (output/(stage+'-representative.spv')).write_bytes(module)
                                    representatives.add(stage)
                            else:
                                token=dxbc_model(payload);stage=model_name(token)[0];value=token
                            require(stage not in result, 'Multiple programs for the same stage')
                            result[stage]=value
                        cached[key]=result
                    return cached[key]
                for render_pass in sub['m_Passes']:
                    for field,value in render_pass.items():
                        if not field.startswith('prog') or not isinstance(value,dict): continue
                        for tier in value.get('m_PlayerSubPrograms',[]):
                            groups=defaultdict(lambda:defaultdict(list))
                            for item in tier:
                                key=(tuple(item['m_KeywordIndices']),item['m_ShaderRequirements'])
                                groups[key][item['m_GpuProgramType']].append(item['m_BlobIndex'])
                            for pair in groups.values():
                                vk=[stages(1,index) for index in pair.get(25,[])]
                                if set(pair)!={25,33} or any(len(x)!=1 for x in pair.values()):
                                    unpaired.update(digest for row in vk for digest,_ in row.values());stats['unpaired']+=1;continue
                                dx=stages(0,pair[33][0]);current=vk[0]
                                require(set(dx)==set(current), 'Paired APIs have different shader stages')
                                for stage,(digest,size) in current.items():
                                    require(digest not in sizes or sizes[digest]==size,'Shader digest has inconsistent length')
                                    sizes[digest]=size;models[digest].add(dx[stage]);stats[stage+'_pairs']+=1
        require(fingerprint()==source['sha256'], 'Source changed during inspection')
    records=[(bytes.fromhex(digest),sizes[digest],next(iter(values))) for digest,values in models.items()
             if len(values)==1 and digest not in unpaired]
    cache=encode(records)
    (output/'shader-models.bin').write_bytes(cache)
    stats.update(unique_modules=len(models),accepted=len(records),unpaired_modules=len(unpaired),
                 conflicting_models=sum(len(x)>1 for x in models.values()),bytes=len(cache))
    report=dict(scope='Exact paired source model evidence; no inferred DXBC hash, code or game acceptance.',counts=dict(stats),
                sources=sources,entries={d:dict(bytes=sizes[d],models=[model_name(v) for v in sorted(values)],
                                              admitted=len(values)==1 and d not in unpaired) for d,values in sorted(models.items())})
    (output/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n',encoding='ascii')
    print(json.dumps(dict(stats),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--packages',type=Path,default=Path('F:/vulkan/unity-analysis/packages'))
    parser.add_argument('--decoder',type=Path,default=ROOT/'build/shader-survey-decoder/smolv_bridge.dll')
    build(parser.parse_args())
