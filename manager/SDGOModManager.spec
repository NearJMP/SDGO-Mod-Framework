# Build from the repository; all inputs are generic code/contracts, never owner state.
from pathlib import Path
import os
import json
import subprocess
import sys
from importlib import metadata
root = Path(SPECPATH).parent
subprocess.run(
    [sys.executable, str(root/'tools/distribution_guard.py'),
     '--source-path', str(root)],
    check=True,
)
sys.path.insert(0,str(root))
from manager.configuration import render
from manager.maps import bundled_example_datas
import pi_heif
if metadata.version('pi-heif-decoder')!='1.7.0.post4' or metadata.version('resvg-py')!='0.5.0':
    raise RuntimeError('Manager codec dependencies must match the audited pinned versions')
heif_info=pi_heif.libheif_info()
if set(heif_info.get('encoders',{}))-{'mask'} or not heif_info.get('decoders',{}).get('libde265'):
    raise RuntimeError('Manager HEIF dependency must be the decoder-only libde265 build')
if heif_info.get('libheif')!='1.23.4':
    raise RuntimeError('Manager HEIF dependency must use verified libheif 1.23.4')
unit_template=Path(workpath)/'unit-catalog-template'/'mod.ini'
unit_template.parent.mkdir(parents=True,exist_ok=True)
unit_template.write_bytes(render(
    (root/'mods/unit_catalog/mod.ini').read_bytes(),
    json.loads((root/'mods/unit_catalog/manager.json').read_bytes()),
))
library = Path(os.environ['SDGO_MANAGER_SERVICES'])
data = bundled_example_datas(root) + [(str(root/'manager/static'),'manager/static'),
        (os.environ.get('SDGO_MANAGER_METADATA',str(root/'manager/repository.json')),'manager'),
        (str(root/'manager/addons'),'manager/addons'),
        (str(root/'manager/engines/operators/resources'),'manager/engines/operators/resources'),
        (str(root/'manager/engines/operators/Install-Operator.ps1'),'manager/engines/operators'),
        (str(root/'owner/stickers/examples'),'owner/stickers/examples'),
        (str(root/'manager/engines/capsules/install_capsule_client.ps1'),'manager/engines/capsules'),
        (str(root/'manager/engines/capsules/resources'),'manager/engines/capsules/resources'),
        (str(root/'manager/engines/shop/bridge.ps1'),'manager/engines/shop'),
        (str(root/'manager/engines/hangar/sort.sql'),'manager/engines/hangar'),
        (str(root/'manager/engines/hangar/gb.sql'),'manager/engines/hangar'),
        (str(root/'manager/engines/hangar/guard.sql'),'manager/engines/hangar'),
        (str(library),'manager/engines/shop'),
        (str(root/'owner/rewards/policy.example.json'),'owner/rewards'),
        (str(root/'owner/rewards/sql'),'owner/rewards/sql'),
        (str(root/'backend/install_ai_catalog_client.ps1'),'backend')]
data += [(str(root/'tools/install_unit_catalog.ps1'),'tools'),
         (str(root/'mods/capsule_catalog/symbol-proposal.ini'),'mods/capsule_catalog'),
         (str(unit_template),'mods/unit_catalog'),
         (str(root/'mods/unit_catalog/default-presets.json'),'mods/unit_catalog'),
         (str(root/'mods/unit_catalog/symbols.fragment.ini'),'mods/unit_catalog'),
         (str(root/'mods/unit_catalog/preview.symbols.fragment.ini'),'mods/unit_catalog'),
         (str(Path(os.environ['SDGO_UNIT_CATALOG_BINARY'])),'unit-import'),
         (str(Path(os.environ['SDGO_CAPSULE_REGISTRY_BINARY'])),'unit-import')]
data += [(str(root/'owner/collection_capsule/sql'),'owner/collection_capsule/sql'),
         (str(root/'owner/calendar_gifts/festival_dates.json'),'owner/calendar_gifts'),
         (str(root/'owner/calendar_gifts/schema.sql'),'owner/calendar_gifts'),
         (str(root/'mods/collection_capsule/assets'),'mods/collection_capsule/assets'),
        (str(Path(os.environ['SDGO_COLLECTION_SUPPORT_BINARY'])),'collection-support'),
         (str(root/'support/capsule_registry'),'support/capsule_registry')]
data += [(str(root/'tools'/name),'tools') for name in ('install_server.ps1','install_common.ps1')]
data += [(str(path),str(path.parent.relative_to(root))) for path in sorted((root/'mods').glob('*/manager.json'))]
python_license=Path(sys.base_prefix)/'LICENSE.txt'
if python_license.is_file():data.append((str(python_license),'licenses/Python'))
codec_license_dir=root/'manager/licenses'
if codec_license_dir.is_dir():data.append((str(codec_license_dir),'licenses/CodecRuntime'))
for package in ('Pillow','pyodbc','PyInstaller','defusedxml','pi-heif-decoder','resvg-py','truststore'):
    try:distribution=metadata.distribution(package)
    except metadata.PackageNotFoundError:continue
    for item in distribution.files or []:
        if ('license' in str(item).lower() or 'copying' in str(item).lower()) and distribution.locate_file(item).is_file():
            data.append((str(distribution.locate_file(item)),'licenses/'+package))
binaries=[]
for package in ('pi-heif-decoder','resvg-py'):
    distribution=metadata.distribution(package)
    for item in distribution.files or []:
        source=distribution.locate_file(item)
        if source.is_file() and source.suffix.lower() in {'.pyd','.dll'}:
            binaries.append((str(source),str(Path(item).parent)))
if any('x265' in Path(source).name.lower() or 'pillow_heif' in Path(source).name.lower() for source,_destination in binaries):
    raise RuntimeError('GPL HEIF encoder binaries are forbidden in Manager')
a = Analysis([str(root/'tools/mod_manager.py')], pathex=[str(root)], datas=data,
             binaries=binaries,
             hiddenimports=['pyodbc','PIL.Image','PIL.ImageDraw','pi_heif','pi_heif.heif','resvg_py','resvg_py.resvg_py','truststore'],
             excludes=['playwright','pillow_heif','_pillow_heif','numpy','scipy','matplotlib','tkinter','pytest'], noarchive=False)
pyz = PYZ(a.pure)
exe = EXE(pyz,a.scripts,a.binaries,a.datas,[],name='SDGO Mod Manager',console=False,
          debug=False,strip=False,upx=False,disable_windowed_traceback=False,
          icon=str(root/'manager/static/brand.ico'))
