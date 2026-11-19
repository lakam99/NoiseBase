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
    Invoke-Check @('--render-ui',(Join-Path $folder ($name+'.png')),'--skin',"$skin") (Join-Path $folder ($name+'.json'))
}
Write-Output 'Native engine, VST3, file workflow and skin render checks passed.'
