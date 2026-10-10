[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
    [Parameter(Mandatory = $true)][string]$ProjectName,
    [Parameter(Mandatory = $true)][string]$JLinkExe,
    [Parameter(Mandatory = $true)][string]$ObjcopyExe,
    [Parameter(Mandatory = $true)][string]$Device,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateRange(1, 50000)][int]$Speed = 1000
)

$ErrorActionPreference = 'Stop'
$WorkspaceRoot = (Resolve-Path -LiteralPath $WorkspaceRoot).Path
$FirmwareElf = Join-Path $WorkspaceRoot "build\$Configuration\$ProjectName.elf"
if (-not (Test-Path -LiteralPath $FirmwareElf -PathType Leaf)) {
    throw "No $Configuration firmware ELF found for '$ProjectName'. Build $Configuration first."
}
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

Write-Host "Device: $Device; configuration: $Configuration; SWD: $Speed kHz"
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
    'exec SetResetType = 3'
    'r'
    'h'
    'sleep 100'
    ('loadfile "{0}", 0x08000000' -f $binary)
    ('verifybin "{0}", 0x08000000' -f $binary)
    'r'
    'sleep 100'
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
