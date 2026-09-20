[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
    [Parameter(Mandatory = $true)][string]$ProjectName,
    [Parameter(Mandatory = $true)][string]$JLinkExe,
    [Parameter(Mandatory = $true)][string]$ObjcopyExe,
    [Parameter(Mandatory = $true)][string]$Device,
    [ValidateRange(1, 50000)][int]$Speed = 1000
)

$ErrorActionPreference = 'Stop'
$WorkspaceRoot = (Resolve-Path -LiteralPath $WorkspaceRoot).Path
$firmwares = @(
    foreach ($configuration in @('Debug', 'Release')) {
        $candidate = Join-Path $WorkspaceRoot "build\$configuration\$ProjectName.elf"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            Get-Item -LiteralPath $candidate
        }
    }
)
if ($firmwares.Count -eq 0) {
    throw "No firmware ELF found for '$ProjectName' in build\Debug or build\Release. Build first."
}
$FirmwareElf = ($firmwares | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).FullName
foreach ($path in @($FirmwareElf, $JLinkExe, $ObjcopyExe)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file not found: $path"
    }
}
$FirmwareElf = (Resolve-Path -LiteralPath $FirmwareElf).Path
$binary = [IO.Path]::ChangeExtension($FirmwareElf, '.bin')
$directory = Split-Path -Parent $FirmwareElf
$scriptPath = Join-Path $directory 'flash.auto.jlink'
$logPath = Join-Path $directory 'jlink-flash.log'

Write-Host "Device: $Device; SWD: $Speed kHz"
Write-Host "Firmware: $FirmwareElf"
if (-not $PSCmdlet.ShouldProcess($Device, "Flash, verify and run $FirmwareElf")) {
    return
}
& $ObjcopyExe -O binary $FirmwareElf $binary
if ($LASTEXITCODE -ne 0) {
    throw "ELF to BIN conversion failed (exit $LASTEXITCODE). Nothing was flashed."
}
if (-not (Test-Path -LiteralPath $binary -PathType Leaf) -or
    (Get-Item -LiteralPath $binary).Length -eq 0) {
    throw "Objcopy did not produce a non-empty BIN: $binary"
}

@(
    'r'
    'h'
    ('loadfile "{0}", 0x08000000' -f $binary)
    ('verifybin "{0}", 0x08000000' -f $binary)
    'r'
    'g'
    'exit'
) | Set-Content -LiteralPath $scriptPath -Encoding Ascii

& $JLinkExe -device $Device -if SWD -speed $Speed -autoconnect 1 -NoGui 1 `
    -ExitOnError 1 -CommandFile $scriptPath | Tee-Object -FilePath $logPath
$result = $LASTEXITCODE
if ($result -ne 0) {
    throw "J-Link failed (exit $result). See $logPath"
}
if (-not (Select-String -LiteralPath $logPath -SimpleMatch 'Verify successful.' -Quiet)) {
    throw "J-Link did not confirm readback verification. See $logPath"
}
Write-Host 'Flash verified; target running.'