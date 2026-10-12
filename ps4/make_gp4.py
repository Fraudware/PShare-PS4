#!/usr/bin/env python3
"""Generate GP4 with directories matching the actual package tree."""
import datetime, pathlib, sys, xml.etree.ElementTree as E
stage=pathlib.Path(sys.argv[1]).resolve(); content_id=sys.argv[2]
project=E.Element('psproject',{'fmt':'gp4','version':'1000'})
volume=E.SubElement(project,'volume')
for key,value in [('volume_type','pkg_ps4_app'),('volume_id','PS4VOLUME'),('volume_ts',datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d %H:%M:%S'))]:
    E.SubElement(volume,key).text=value
E.SubElement(volume,'package',{'content_id':content_id,'passcode':'0'*32,'storage_type':'digital50','app_type':'full'})
chunks=E.SubElement(volume,'chunk_info',{'chunk_count':'1','scenario_count':'1'})
E.SubElement(E.SubElement(chunks,'chunks'),'chunk',{'id':'0','layer_no':'0','label':'Chunk #0'})
E.SubElement(E.SubElement(chunks,'scenarios',{'default_id':'0'}),'scenario',{'id':'0','type':'sp','initial_chunk_count':'1','label':'Scenario #0'}).text='0'
files=E.SubElement(project,'files',{'img_no':'0'})
root=E.SubElement(project,'rootdir'); directories={():root}
paths=[stage/'eboot.bin']
for name in ('assets','sce_sys','sce_module'): paths.extend(p for p in (stage/name).rglob('*') if p.is_file())
for path in sorted(paths):
    relative=path.relative_to(stage)
    E.SubElement(files,'file',{'targ_path':relative.as_posix(),'orig_path':relative.as_posix()})
    for n in range(1,len(relative.parts)):
        parts=relative.parts[:n]
        if parts not in directories:
            directories[parts]=E.SubElement(directories[parts[:-1]],'dir',{'targ_name':parts[-1]})
E.indent(project)
E.ElementTree(project).write(stage/'pshare.gp4',encoding='utf-8',xml_declaration=True)
