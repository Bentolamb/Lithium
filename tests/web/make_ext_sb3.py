"""Builds ext_test.sb3 from render_test.sb3: a stage script using Packin, OutList and Lithium Lab."""
import json, zipfile, sys
src = zipfile.ZipFile('render_test.sb3')
proj = json.loads(src.read('project.json'))
stage = proj['targets'][0]
def lit(s): return [1, [10, s]]
def rep(i): return [3, i, [10, '']]
def setv(var, val, nxt, parent):
    return {'opcode': 'data_setvariableto', 'next': nxt, 'parent': parent, 'inputs': {'VALUE': val}, 'fields': {'VARIABLE': [var, var]}}
for v in ('xr', 'xe', 'xx', 'xl'): stage['variables'][v] = [v, '']
stage['lists']['XL'] = ['XL', ['a', 'b', 'c']]
b = stage['blocks']
b['e_h'] = {'opcode': 'event_whenflagclicked', 'next': 'e_save', 'parent': None, 'inputs': {}, 'fields': {}, 'topLevel': True, 'x': 600, 'y': 0}
b['e_save'] = {'opcode': 'outlist_saveList', 'next': 'e_clr', 'parent': 'e_h', 'inputs': {'NAME': lit('gui test')}, 'fields': {'LIST': ['XL', None]}}
b['e_clr'] = {'opcode': 'data_deletealloflist', 'next': 'e_load', 'parent': 'e_save', 'inputs': {}, 'fields': {'LIST': ['XL', 'XL']}}
b['e_load'] = {'opcode': 'outlist_loadList', 'next': 's1', 'parent': 'e_clr', 'inputs': {'NAME': lit('gui test')}, 'fields': {'LIST': ['XL', None]}}
b['s1'] = setv('xx', rep('dec'), 's2', 'e_load')
b['dec'] = {'opcode': 'packin_decompress', 'next': None, 'parent': 's1', 'inputs': {'DATA': rep('cmp')}, 'fields': {}}
b['cmp'] = {'opcode': 'packin_compress', 'next': None, 'parent': 'dec', 'inputs': {'DATA': lit('hello hello hello hello')}, 'fields': {}}
b['s2'] = setv('xr', rep('ex'), 's3', 's1')
b['ex'] = {'opcode': 'outlist_exists', 'next': None, 'parent': 's2', 'inputs': {'NAME': lit('gui test')}, 'fields': {}}
b['s3'] = setv('xe', rep('lab'), 's4', 's2')
b['lab'] = {'opcode': 'lithiumlab_isLithium', 'next': None, 'parent': 's3', 'inputs': {}, 'fields': {}}
b['s4'] = setv('xl', rep('tw'), 'done', 's3')
b['tw'] = {'opcode': 'lithiumlab_isTurboWarp', 'next': None, 'parent': 's4', 'inputs': {}, 'fields': {}}
b['done'] = {'opcode': 'data_setvariableto', 'next': None, 'parent': 's4', 'inputs': {'VALUE': lit('1')}, 'fields': {'VARIABLE': ['ext_done', 'ext_done']}}
stage['variables']['ext_done'] = ['ext_done', 0]
proj['extensions'] = sorted(set(proj.get('extensions', [])) | {'outlist', 'packin', 'lithiumlab'})
out = zipfile.ZipFile('ext_test.sb3', 'w', zipfile.ZIP_DEFLATED)
for n in src.namelist():
    if n != 'project.json': out.writestr(n, src.read(n))
out.writestr('project.json', json.dumps(proj))
out.close()
