param(
    [ValidateSet('Check','Prepare')][string]$Mode = 'Check',
    [Parameter(Mandatory=$true)][string]$CurrentVersion,
    [string]$Report
)
. (Join-Path $PSScriptRoot 'Release-Common.ps1')
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
try {
    $headers = @{ 'User-Agent'=('NoiseBase/' + $CurrentVersion); Accept='application/vnd.github+json'; 'X-GitHub-Api-Version'='2022-11-28' }
    $response = Invoke-WebRequest -Uri 'https://api.github.com/repos/lakam99/NoiseBase/releases/latest' -Headers $headers -UseBasicParsing -TimeoutSec 20
    if ($response.Content.Length -gt 2097152) { throw 'Release metadata is too large.' }
    $release = Get-NoiseBaseRelease ($response.Content | ConvertFrom-Json) $CurrentVersion
    $result = [ordered]@{ passed=$true; available=$release.available; version=$release.version; releaseUrl=$release.releaseUrl }
    if ($Mode -eq 'Prepare' -and $release.available) {
        $cache = Join-Path $env:LOCALAPPDATA ('NoiseBase/Updates/' + $release.version + '-' + [guid]::NewGuid().ToString('N'))
        $null = New-Item -ItemType Directory -Path $cache -Force
        $archive = Join-Path $cache $release.name
        $sums = Invoke-WebRequest -Uri $release.sums.browser_download_url -Headers @{'User-Agent'=$headers['User-Agent']} -UseBasicParsing -TimeoutSec 20
        $expected = Get-ReleaseChecksum $sums.Content $release.name $release.asset.digest
        Invoke-WebRequest -Uri $release.asset.browser_download_url -Headers @{'User-Agent'=$headers['User-Agent']} -UseBasicParsing -TimeoutSec 240 -OutFile $archive
        if ((Get-Item -LiteralPath $archive).Length -ne $release.asset.size -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $expected) { throw 'Downloaded package failed SHA-256 or size verification.' }
        $expanded = Join-Path $cache 'expanded'
        $null = New-Item -ItemType Directory -Path $expanded
        Expand-VerifiedArchive $archive $expanded
        $package = Join-Path $expanded 'NoiseBase'
        $manifest = Get-VerifiedPackage $package
        if ($manifest.version -cne $release.version) { throw 'Package version does not match the release tag.' }
        # Run the existing local installer from outside the directory it replaces.
        $bootstrap = Join-Path $cache 'bootstrap'
        $null = New-Item -ItemType Directory -Path $bootstrap
        foreach ($name in @('Install-NoiseBase.ps1','Release-Common.ps1')) {
            Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $bootstrap $name)
        }
        $result.packageRoot = $package
        $result.installer = Join-Path $bootstrap 'Install-NoiseBase.ps1'
    }
    if ($Report) { $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Report -Encoding UTF8 }
    else { $result | ConvertTo-Json -Depth 8 }
} catch {
    $failure = [ordered]@{ passed=$false; error=$_.Exception.Message }
    if ($Report) { $failure | ConvertTo-Json | Set-Content -LiteralPath $Report -Encoding UTF8 }
    else { $failure | ConvertTo-Json }
    exit 1
}
