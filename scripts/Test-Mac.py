"""Run native Apple Silicon verification without opening the user's audio device."""
from pathlib import Path
import json, math, platform, random, struct, subprocess, uuid, wave

root=Path(__file__).resolve().parent.parent
build=root/'build'
app=build/'NoiseBaseStandalone_artefacts/Release/NoiseBase.app/Contents/MacOS/NoiseBase'
plugin=build/'NoiseBase_artefacts/Release/VST3/NoiseBase.vst3'
reports=build/'verification-mac'
reports.mkdir(exist_ok=True)
assert platform.machine()=='arm64',platform.machine()

def check(name,*args):
    report=reports/(name+'.json')
    command=[str(app),*map(str,args),'--report',str(report)]
    try:subprocess.run(command,check=True,timeout=120)
    except subprocess.CalledProcessError:
        subprocess.run(['lldb','--batch','-o','run','-k','thread backtrace all','--',*command],timeout=180)
        raise
    result=json.loads(report.read_text())
    assert result.get('passed'),result
    return result

check('engine','--self-test')
check('theme-icon','--theme-test')
check('vst3','--validate-plugin',plugin,'--snapshot',reports/'vst3-host.png','--cache-state',reports/'session-cache.bin')
check('cache-cold-start','--restore-cached-plugin',plugin,'--cache-state',reports/'session-cache.bin','--snapshot',reports/'cache-restored.png')
check('cache-cold-start-bridge','--restore-cached-plugin',plugin,'--cache-state',reports/'session-cache-bridge.bin')
check('sidechain-ui','--render-ui',reports/'sidechain.png','--plugin-layout','--procedural','--sidechain','--skin',2)
check('gap-bridge-ui','--render-ui',reports/'gap-bridge.png','--plugin-layout','--procedural','--sidechain','--bridge','--reference-preview','--skin',2)
fixture=build/'fixture.wav'
rng=random.Random(417)
with wave.open(str(fixture),'wb') as file:
    file.setparams((1,2,44100,0,'NONE','not compressed'))
    values=[]
    for i in range(44100*8):
        t=i/44100
        amplitude=.06 if 1<t<3 or 4<t<6 else 0
        value=rng.gauss(0,.002)+amplitude*math.sin(2*math.pi*170*t)*(1+.4*math.sin(2*math.pi*3*t))
        values.append(round(max(-1,min(1,value))*32767))
    file.writeframes(struct.pack('<'+'h'*len(values),*values))
check('workflow','--ui-smoke',fixture,build/f'mac-export-{uuid.uuid4().hex}.wav','--snapshot',reports/'loaded.png','--skin',2)
for skin,name in enumerate(('ivory','graphite','walnut')):
    check(name,'--render-ui',reports/(name+'.png'),'--procedural','--skin',skin)
procedural=build/f'mac-procedural-60-{uuid.uuid4().hex}.wav'
check('procedural-export','--process',fixture,procedural,'--procedural','--seconds',60)
with wave.open(str(procedural)) as file:
    assert file.getnframes()==44100*60 and file.getnchannels()==1
for bundle in (app.parents[2],plugin):
    subprocess.run(['codesign','--verify','--deep','--strict',str(bundle)],check=True)
    binary=app if bundle.suffix=='.app' else bundle/'Contents/MacOS/NoiseBase'
    assert subprocess.check_output(['lipo','-archs',str(binary)],text=True).strip()=='arm64'
    subprocess.run(['otool','-L',str(bundle/'Contents/Frameworks/libonnxruntime.dylib')],check=True)
(reports/'platform.json').write_text(json.dumps({'passed':True,'architecture':platform.machine(),'macos':platform.mac_ver()[0],'developer_id_signed':False,'notarized':False,'minimum_macos':'14.0'},indent=2))
print('Apple Silicon engine, VST3, profile persistence, file workflow, 60-second procedural export, UI, icon and ad-hoc signatures passed.')
