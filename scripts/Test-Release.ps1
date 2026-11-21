param([string]$Build = (Join-Path $PSScriptRoot '../build'))
$ErrorActionPreference='Stop'
$Build=[IO.Path]::GetFullPath($Build)
$app=Join-Path $Build 'NoiseBaseStandalone_artefacts/Release/NoiseBase.exe'
$plugin=Join-Path $Build 'NoiseBase_artefacts/Release/VST3/NoiseBase.vst3'
$folder=Join-Path $Build 'verification'
$null=New-Item -ItemType Directory -Path $folder -Force
function Invoke-Check([string[]]$Arguments,[string]$Report) {
    $quoted=@($Arguments | ForEach-Object {'"'+$_+'"'}) + @('--report',('"'+$Report+'"'))
    $process=Start-Process -FilePath $app -ArgumentList $quoted -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0 -or -not (Get-Content -LiteralPath $Report -Raw | ConvertFrom-Json).passed) { throw ('Native verification failed: ' + [IO.Path]::GetFileName($Report)) }
}
Invoke-Check @('--self-test') (Join-Path $folder 'engine.json')
Invoke-Check @('--theme-test') (Join-Path $folder 'window-theme-icon.json')
Invoke-Check @('--render-ui',(Join-Path $folder 'update-downloading.png'),'--skin','2','--update-progress','0.42') (Join-Path $folder 'update-downloading.json')
Invoke-Check @('--render-ui',(Join-Path $folder 'update-verifying.png'),'--skin','2','--update-progress','-1','--update-phase','VERIFYING') (Join-Path $folder 'update-verifying.json')
Invoke-Check @('--validate-plugin',$plugin,'--snapshot',(Join-Path $Build 'host-wrapper.png')) (Join-Path $folder 'vst3.json')
$fixture=Join-Path $Build 'fixture.wav'
@'
import math,random,struct,sys,wave
rng=random.Random(417)
with wave.open(sys.argv[1],'wb') as f:
 f.setparams((1,2,44100,0,'NONE','not compressed'))
 for i in range(44100*8):
  t=i/44100
  amplitude=.06 if 1<t<3 or 4<t<6 else 0
  value=rng.gauss(0,.002)+amplitude*math.sin(2*math.pi*170*t)*(1+.4*math.sin(2*math.pi*3*t))
  f.writeframesraw(struct.pack('<h',round(max(-1,min(1,value))*32767)))
'@ | python - $fixture
if ($LASTEXITCODE -ne 0) {throw 'Fixture creation failed.'}
$export=Join-Path $Build ('fixture-export-'+[guid]::NewGuid().ToString('N')+'.wav')
Invoke-Check @('--ui-smoke',$fixture,$export,'--snapshot',(Join-Path $folder 'loaded.png'),'--skin','2') (Join-Path $folder 'file-workflow.json')
foreach ($skin in 0..2) {
    $name=@('ivory','graphite','walnut')[$skin]
    # Exercise replacement of an existing preview, rather than appending to it.
    [IO.File]::WriteAllBytes((Join-Path $folder ($name+'.png')),[Text.Encoding]::ASCII.GetBytes('obsolete preview'))
    Invoke-Check @('--render-ui',(Join-Path $folder ($name+'.png')),'--skin',"$skin") (Join-Path $folder ($name+'.json'))
    Invoke-Check @('--render-ui',(Join-Path $folder ($name+'-vst3.png')),'--plugin-layout','--skin',"$skin") (Join-Path $folder ($name+'-vst3.json'))
    Invoke-Check @('--render-ui',(Join-Path $folder ($name+'-noise-only.png')),'--noise-only','--skin',"$skin") (Join-Path $folder ($name+'-noise-only.json'))
    Invoke-Check @('--render-ui',(Join-Path $folder ($name+'-vst3-noise-only.png')),'--plugin-layout','--noise-only','--skin',"$skin") (Join-Path $folder ($name+'-vst3-noise-only.json'))
    Invoke-Check @('--render-ui',(Join-Path $folder ($name+'-procedural.png')),'--procedural','--skin',"$skin") (Join-Path $folder ($name+'-procedural.json'))
    Invoke-Check @('--render-ui',(Join-Path $folder ($name+'-vst3-procedural.png')),'--plugin-layout','--procedural','--skin',"$skin") (Join-Path $folder ($name+'-vst3-procedural.json'))
}
@'
from pathlib import Path
import struct,sys
for path in Path(sys.argv[1]).glob('*.png'):
 data=path.read_bytes()
 assert data[:8]==b'\x89PNG\r\n\x1a\n',str(path)
 offset=8
 while offset<len(data):
  size=struct.unpack('>I',data[offset:offset+4])[0]
  kind=data[offset+4:offset+8]
  offset+=size+12
  if kind==b'IEND':break
 assert kind==b'IEND' and offset==len(data),'Stale or concatenated PNG: '+str(path)
'@ | python - $folder
if ($LASTEXITCODE -ne 0) {throw 'Preview replacement validation failed.'}
Write-Output 'Native engine, VST3, file workflow and skin render checks passed.'
