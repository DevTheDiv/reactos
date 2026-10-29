<#
Prepare a pinned Windows Venus host candidate beside the existing QEMU install.
Extracts the release with 7-Zip; never executes the installer or changes PATH.
#>
[CmdletBinding()]
param(
    [string]$ArchivePath,
    [string]$Destination,
    [string]$SevenZip
)

$ErrorActionPreference = 'Stop'
$releaseTag = 'alpha10'
$releaseUrl = 'https://github.com/cmspam/winq-emu/releases/tag/alpha10'
$assetUrl = 'https://github.com/cmspam/winq-emu/releases/download/alpha10/WINQ-EMU-Alpha10-Setup.exe'
$expectedHash = '7a50aa4fac7a53ef911de2c9db756fbc0b2685dd5522f2a4591910c520f15fbe'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$workspaceRoot = Split-Path $sourceRoot -Parent
if (!$Destination) {
    $Destination = Join-Path $workspaceRoot 'tools/graphics-host/winq-alpha10'
}
$Destination = [IO.Path]::GetFullPath($Destination)

if (Test-Path -LiteralPath $Destination) {
    throw "Destination already exists: $Destination. Choose another -Destination; existing files will not be replaced."
}
if (!$SevenZip) {
    $standard7Zip = Join-Path ${env:ProgramFiles} '7-Zip/7z.exe'
    if (Test-Path -LiteralPath $standard7Zip -PathType Leaf) {
        $SevenZip = $standard7Zip
    } else {
        $command7Zip = Get-Command 7z.exe -ErrorAction SilentlyContinue
        if ($command7Zip) { $SevenZip = $command7Zip.Source }
    }
}
if (!$SevenZip -or !(Test-Path -LiteralPath $SevenZip -PathType Leaf)) {
    throw '7-Zip is required to extract the NSIS archive without installing it. Supply -SevenZip with the path to 7z.exe.'
}

$destinationParent = Split-Path $Destination -Parent
if (!$ArchivePath) {
    $downloadDirectory = Join-Path $destinationParent 'downloads'
    New-Item -ItemType Directory -Path $downloadDirectory -Force | Out-Null
    $ArchivePath = Join-Path $downloadDirectory 'WINQ-EMU-Alpha10-Setup.exe'
    if (!(Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
        $partialPath = Join-Path $downloadDirectory ('download-' + [Guid]::NewGuid().ToString('N') + '.partial')
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $assetUrl -OutFile $partialPath -TimeoutSec 120
        } catch {
            throw "Release download failed: $($_.Exception.Message) Download $assetUrl in a network-enabled terminal, or rerun with -ArchivePath pointing to that release."
        }
        $downloadHash = (Get-FileHash -LiteralPath $partialPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($downloadHash -ne $expectedHash) {
            throw "Downloaded SHA-256 mismatch. File retained at $partialPath; nothing was extracted."
        }
        Move-Item -LiteralPath $partialPath -Destination $ArchivePath
    }
}
$ArchivePath = (Resolve-Path -LiteralPath $ArchivePath).Path
$archiveHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($archiveHash -ne $expectedHash) {
    throw "Release SHA-256 mismatch: expected $expectedHash, got $archiveHash. Nothing was extracted."
}

New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
$stagingPath = Join-Path $destinationParent ('extract-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stagingPath | Out-Null
& $SevenZip x -y "-o$stagingPath" $ArchivePath | Out-File -LiteralPath (Join-Path $stagingPath 'extraction.log')
if ($LASTEXITCODE -ne 0) {
    throw "7-Zip extraction failed ($LASTEXITCODE). Files retained at $stagingPath."
}
$qemuCandidates = @(Get-ChildItem -LiteralPath $stagingPath -Recurse -File -Filter 'qemu-system-x86_64.exe')
if ($qemuCandidates.Count -ne 1) {
    throw "Expected one QEMU executable; found $($qemuCandidates.Count). Inspect $stagingPath."
}
$qemuFile = $qemuCandidates[0]
$rendererPath = Join-Path $qemuFile.DirectoryName 'libvirglrenderer-1.dll'
$imageToolPath = Join-Path $qemuFile.DirectoryName 'qemu-img.exe'
$firmwarePath = Join-Path $qemuFile.DirectoryName 'share'
if (!(Test-Path -LiteralPath $rendererPath -PathType Leaf) -or
    !(Test-Path -LiteralPath $imageToolPath -PathType Leaf) -or
    !(Test-Path -LiteralPath $firmwarePath -PathType Container)) {
    throw "Release layout lacks renderer, image tool, or firmware. Inspect $stagingPath."
}
$qemuRelative = $qemuFile.FullName.Substring($stagingPath.Length + 1)
$manifest = [ordered]@{
    schema_version = 1
    prepared_at_utc = [DateTime]::UtcNow.ToString('o')
    project = 'cmspam/winq-emu'
    release_tag = $releaseTag
    release_url = $releaseUrl
    asset_url = $assetUrl
    archive_sha256 = $archiveHash
    qemu_relative_path = $qemuRelative
    qemu_sha256 = (Get-FileHash -LiteralPath $qemuFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    renderer_sha256 = (Get-FileHash -LiteralPath $rendererPath -Algorithm SHA256).Hash.ToLowerInvariant()
    renderer_runtime_validated = $false
    guest_vulkan_validated = $false
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $stagingPath 'host-package.json') -Encoding UTF8
Move-Item -LiteralPath $stagingPath -Destination $Destination
Write-Output "Prepared candidate: $(Join-Path $Destination $qemuRelative)"
Write-Output "Manifest: $(Join-Path $Destination 'host-package.json')"
Write-Output 'Extraction is complete. Renderer initialization and guest Vulkan still require active VM tests.'
