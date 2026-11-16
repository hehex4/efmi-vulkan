"""Read-only helpers for the observed 2021.3 shader asset schema.

This is an offline evidence reader, not a runtime compatibility map. Unknown
schemas, signatures or channel relations fail closed. Asset payloads stay local.
"""
import ctypes
import hashlib
import struct


class NonVertexProgram(ValueError):
    """A decoded module belongs to a different shader stage."""


def require(condition, message):
    if not condition:
        raise ValueError(message)


def span(data, offset, size):
    require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset,
            f"Range {offset}+{size} exceeds {len(data)} bytes")
    return data[offset:offset + size]


def u32(data, offset=0):
    return struct.unpack('<I', span(data, offset, 4))[0]


def shader_tree(original):
    # UnityPy's standard schema omits the engine's descriptor-set metadata and
    # inline per-LOD binaries. Clone it; never mutate its shared type database.
    from UnityPy.helpers.TypeTreeNode import TypeTreeNode as Node

    def node(kind, name, size=-1, children=None, align=0):
        return Node(0, kind, name, size, 1, children or [], m_MetaFlag=align)

    def integer(name):
        return node('int', name, 4)

    def vector(name, item):
        return node('vector', name, children=[node('Array', 'Array',
                    children=[integer('size'), item], align=0x4000)])

    def clone(item):
        return Node(item.m_Level, item.m_Type, item.m_Name, item.m_ByteSize,
                    item.m_Version, [clone(c) for c in item.m_Children],
                    m_MetaFlag=item.m_MetaFlag)

    tree = clone(original)
    for item in list(tree.traverse()):
        if item.m_Type == 'SerializedProgramParameters':
            binding = node('SetBinding', 'data', children=[integer(x) for x in
                ['m_NameIndex', 'm_BindingIndex', 'm_DescriptorType']] +
                [node('unsigned int', x, 4) for x in ['m_PackedBinding', 'm_PackedInfo']])
            descriptor = node('DescriptorSetParam', 'data', children=[integer(x)
                for x in ['m_NameIndex', 'm_SetId', 'm_MaxBindingIndex']] +
                [vector('m_Bindings', binding)])
            item.m_Children = item.m_Children + [vector('m_DescriptorSetParams', descriptor)]
        elif item.m_Type == 'SerializedShader':
            children = []
            for child in item.m_Children:
                children.append(child)
                if child.m_Name == 'm_KeywordNames':
                    extra = clone(child)
                    extra.m_Name = 'm_DifferentMaterialCbKeywordNames'
                    children.append(extra)
            item.m_Children = children
        elif item.m_Type == 'SerializedPass':
            children = []
            for child in item.m_Children:
                children.append(child)
                if child.m_Name == 'm_HasProceduralInstancingVariant':
                    # Construct the unaligned node directly: the native reader
                    # caches alignment when a TypeTreeNode is constructed.
                    children[-1] = node('bool', child.m_Name, 1)
                    children.extend([node('bool', 'm_HasSRPInstancingVariant', 1),
                                     node('bool', 'm_HasHGECSInstancingVariant', 1, align=0x4000)])
            item.m_Children = children
    children = []
    for child in tree.m_Children:
        children.append(child)
        if child.m_Name == 'm_ParsedForm':
            children.append(node('bool', 'm_EnableShaderLODStreaming', 1, align=0x4000))
            chunk = node('SubShaderBlob', 'data', children=[integer('m_ShaderLOD'),
                vector('m_CompressedBlob', node('UInt8', 'data', 1))] +
                [vector(x, vector('data', node('unsigned int', 'data', 4)))
                 for x in ['m_Offsets', 'm_CompressedLengths', 'm_DecompressedLengths']])
            children.append(vector('subShaderBlobs', chunk))
        if child.m_Name == 'stageCounts':
            children.append(integer('m_CompressionType'))
    tree.m_Children = children
    return tree


def program(segments, index, expected_type):
    table = segments[0]
    count = u32(table)
    require(0 <= index < count and count <= (len(table) - 4) // 12, 'Invalid program table')
    offset, size, segment = struct.unpack('<3I', span(table, 4 + index * 12, 12))
    require(segment < len(segments), 'Invalid program segment')
    data = span(segments[segment], offset, size)
    require(u32(data) == 202506210 and u32(data, 4) == expected_type, 'Unknown program schema/type')
    pos, count = 28, u32(data, 24)
    require(count <= (len(data) - pos) // 4, 'Invalid keyword count')
    for _ in range(count):
        size = u32(data, pos)
        span(data, pos + 4, size).decode('utf-8')
        pos = (pos + 4 + size + 3) & ~3
    size = u32(data, pos)
    code = span(data, pos + 4, size)
    end = (pos + 4 + size + 3) & ~3
    tail = span(data, end, len(data) - end)
    require(len(tail) % 4 == 0, 'Unaligned channel metadata')
    return code, list(struct.unpack('<' + str(len(tail) // 4) + 'I', tail))


class Smolv:
    def __init__(self, library):
        self.library = ctypes.CDLL(str(library))
        self.library.smolv_size.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        self.library.smolv_size.restype = ctypes.c_size_t
        self.library.smolv_decode.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                            ctypes.c_void_p, ctypes.c_size_t]
        self.library.smolv_decode.restype = ctypes.c_bool

    def vertex(self, code):
        offset, size = struct.unpack('<2I', span(code, 4, 8))
        data = span(code, offset, size)
        decoded_size = self.library.smolv_size(data, len(data))
        require(20 <= decoded_size <= 64 * 1024 * 1024, 'Invalid SMOL-V decoded size')
        output = ctypes.create_string_buffer(decoded_size)
        require(self.library.smolv_decode(data, len(data), output, decoded_size), 'SMOL-V decode failed')
        return output.raw, vertex_locations(output.raw)


def vertex_locations(data):
    require(len(data) >= 20 and len(data) % 4 == 0 and u32(data) == 0x07230203, 'Invalid SPIR-V header')
    words = struct.unpack('<' + str(len(data) // 4) + 'I', data)
    pos, inputs, locations, names, models = 5, set(), {}, {}, []
    while pos < len(words):
        count, opcode = words[pos] >> 16, words[pos] & 65535
        require(count and pos + count <= len(words), 'Invalid SPIR-V instruction length')
        args = words[pos + 1:pos + count]
        if opcode in (15, 59):
            require(len(args) >= 3, 'Truncated SPIR-V interface instruction')
        if opcode == 71:
            require(len(args) >= 2 and (args[1] != 30 or len(args) >= 3), 'Truncated SPIR-V decoration')
        if opcode == 15:
            models.append(args[0])
        elif opcode == 59 and args[2] == 1:
            inputs.add(args[1])
        elif opcode == 71 and args[1] == 30:
            locations[args[0]] = args[2]
        elif opcode == 5:
            require(len(args) >= 2, 'Truncated SPIR-V name')
            names[args[0]] = struct.pack('<' + str(len(args) - 1) + 'I', *args[1:]).split(b'\0')[0].decode('utf-8')
        pos += count
    require(len(models) == 1, 'Expected exactly one shader entry point')
    if models != [0]:
        raise NonVertexProgram(f'Expected one vertex entry point, found {models}')
    result = sorted((locations[i], names.get(i, '')) for i in inputs if i in locations)
    require(len({loc for loc, _ in result}) == len(result), 'Ambiguous vertex locations')
    return result


def signature(code):
    offset, size = struct.unpack('<2I', span(code, 4, 8))
    data = span(code, offset, size)
    require(data[:4] == b'DXBC' and u32(data, 24) == len(data), 'Invalid DXBC container')
    count, records, vertex, signatures = u32(data, 28), [], False, 0
    require(count <= (len(data) - 32) // 4, 'Invalid DXBC chunk count')
    for index in range(count):
        pos = u32(data, 32 + 4 * index)
        kind = span(data, pos, 4)
        chunk = span(data, pos + 8, u32(data, pos + 4))
        if kind in (b'SHDR', b'SHEX'):
            vertex = (u32(chunk) >> 16) == 1
        if kind in (b'ISGN', b'ISG1'):
            signatures += 1
            stride = 32 if kind == b'ISG1' else 24
            require(u32(chunk) <= (len(chunk) - 8) // stride, 'Invalid signature record count')
            for entry in range(u32(chunk)):
                record = 8 + entry * stride + (4 if kind == b'ISG1' else 0)
                name, semantic_index, system, value_type, register = struct.unpack('<5I', span(chunk, record, 20))
                require(name < len(chunk), 'Invalid signature string offset')
                end = chunk.index(0, name)
                records.append(dict(semantic=chunk[name:end].decode('ascii'), index=semantic_index,
                                    system=system, type=value_type, register=register, mask=chunk[record + 20],
                                    read_mask=chunk[record + 21]))
    require(vertex and signatures == 1, 'Expected one vertex input signature')
    return hashlib.sha256(data).hexdigest(), records


def channel_semantics(tail, locations, inputs):
    require(len(tail) >= 2 and len(tail) == 2 + 2 * tail[1], 'Invalid channel table length')
    channels = dict(zip(tail[2::2], tail[3::2]))
    require(len(channels) == tail[1] and all(0 <= c < 14 for c in channels), 'Unknown/duplicate source channel')
    require(sum(1 << c for c in channels) == tail[0], 'Channel mask differs from its records')
    require(len(set(channels.values())) == len(channels), 'Duplicate channel target')
    # The source IDs agree with Unity VertexAttribute. Target-minus-13 is an
    # observed compiler encoding, checked against every actual SPIR-V input.
    # This survey does not install that inferred encoding into the runtime.
    require({target - 13 for target in channels.values()} == {loc for loc, _ in locations},
            'Channel targets do not cover the SPIR-V locations')
    mapped = []
    base = dict(POSITION=0, NORMAL=1, TANGENT=2, COLOR=3,
                BLENDWEIGHT=12, BLENDWEIGHTS=12, BLENDINDICES=13)
    for item in inputs:
        if item['system']:
            continue
        semantic, index = item['semantic'].upper(), item['index']
        channel = (4 + index if semantic == 'TEXCOORD' and index < 8 else
                   base.get(semantic) if index == 0 else None)
        require(channel in channels, 'Signature semantic has no recognized source channel')
        mapped.append((channels[channel] - 13, item['semantic'], index))
    require(len(mapped) == len(channels) and len({r[0] for r in mapped}) == len(mapped),
            'Signature does not cover each source channel once')
    return sorted(mapped)
