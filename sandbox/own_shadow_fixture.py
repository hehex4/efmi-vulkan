"""A visible ordinary mesh with valid inputs for the captured pipeline family."""
import struct


def mesh(mod, original):
    # Owned test geometry: a box around the recorded object origin. No captured
    # character topology, merged bone indices or inferred remapping is used.
    vertices = [(x, y, z) for z in (0.0, 1.8) for y in (-0.65, 0.65) for x in (-0.65, 0.65)]
    triangles = (0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6,
                 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7,
                 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5)
    (mod / 'own-position.buf').write_bytes(b''.join(struct.pack('<3fI', *p, 0) for p in vertices))
    (mod / 'own-uv.buf').write_bytes(struct.pack('<2fI', 0.5, 0.5, 0) * len(vertices))
    (mod / 'own-blend.buf').write_bytes(struct.pack('<4H4B', 65535, 0, 0, 0, 0, 0, 0, 0) * len(vertices))
    (mod / 'own-index.buf').write_bytes(struct.pack('<36H', *triangles))
    start = original.index('[CommandList_Draw_Component1]\n')
    end = original.index('[CommandList_Draw_Component2]\n', start)
    body = r'''[CommandList_Draw_Component1]
ib = ResourceOwnIndex
vb0 = ResourceOwnPosition
vb3 = ResourceOwnPosition
vb1 = ResourceOwnUV
vb2 = ResourceOwnBlend
$\EFMITTL\alpha = 0.7
$\EFMITTL\count = 36
$\EFMITTL\first = 0
    run = CommandList\EFMITTL\Draw

'''
    resources = '''
[ResourceOwnPosition]
type = Buffer
stride = 16
filename = own-position.buf
[ResourceOwnUV]
type = Buffer
stride = 12
filename = own-uv.buf
[ResourceOwnBlend]
type = Buffer
stride = 12
filename = own-blend.buf
[ResourceOwnIndex]
type = Buffer
format = R16_UINT
filename = own-index.buf
'''
    return original[:start] + body + original[end:] + resources
