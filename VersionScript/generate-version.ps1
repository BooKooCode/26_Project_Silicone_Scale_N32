param(
    [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
    [Parameter(Mandatory = $true)][string]$OutputFile,
    [Parameter(Mandatory = $true)][string]$GitExe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-VersionGit {
    param([string[]]$GitArguments, [switch]$AllowFailure)

    try {
        $result = & $GitExe -C $WorkspaceRoot @GitArguments 2>&1
    }
    catch {
        if ($AllowFailure) {
            return $null
        }
        throw
    }
    if ($LASTEXITCODE -ne 0) {
        if ($AllowFailure) {
            return $null
        }
        throw "Git failed: $($GitArguments -join ' ')"
    }
    return (($result | ForEach-Object { "$_" }) -join "`n").Trim()
}

function ConvertTo-VersionLiteral {
    param([string]$Name, [string]$Value)

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Value)
    if ($bytes.Length -gt 31) {
        throw "$Name exceeds 31 UTF-8 bytes; cannot fit the 32-byte field."
    }
    if ($Value.IndexOf([char]0) -ge 0) {
        throw "$Name contains a NUL character."
    }
    $escaped = ($bytes | ForEach-Object {
        '\' + [Convert]::ToString($_, 8).PadLeft(3, '0')
    }) -join ''
    return '"' + $escaped + '"'
}

try {
    $head = Invoke-VersionGit -GitArguments @('rev-parse', '--verify', 'HEAD')
    $shortHash = Invoke-VersionGit -GitArguments @('rev-parse', '--short=7', 'HEAD')
    $tag = Invoke-VersionGit -GitArguments @('describe', '--tags', '--abbrev=0') -AllowFailure
    if ([string]::IsNullOrWhiteSpace($tag)) {
        $version = "no-tag-$shortHash"
    }
    else {
        $tagCommit = Invoke-VersionGit -GitArguments @('rev-parse', "$tag^{commit}")
        $version = $tag
        if ($tagCommit -ne $head) {
            $version += "-$shortHash"
        }
    }

    $dirty = Invoke-VersionGit -GitArguments @(
        'status', '--porcelain', '--untracked-files=all', '--', '.',
        ':(exclude)build', ':(exclude)docs', ':(exclude,glob)**/*.md'
    )
    if (-not [string]::IsNullOrWhiteSpace($dirty)) {
        $version += '-modified'
    }

    $commitTimestamp = Invoke-VersionGit -GitArguments @('log', '-1', '--format=%ct')
    $beijingOffset = [TimeSpan]::FromHours(8)
    $commitTime = [DateTimeOffset]::FromUnixTimeSeconds([long]$commitTimestamp).ToOffset($beijingOffset)
    $compileTime = [DateTimeOffset]::UtcNow.ToOffset($beijingOffset)
    $email = Invoke-VersionGit -GitArguments @('config', 'user.email')
    if ([string]::IsNullOrWhiteSpace($email)) {
        throw 'Git user.email is empty; configure the build identity before building.'
    }

    $culture = [System.Globalization.CultureInfo]::InvariantCulture
    $fields = @(
        @{ Name = 'TAG_VERSION'; Address = '0800F000'; Value = $version },
        @{ Name = 'COMMIT_TIME'; Address = '0800F020'; Value = $commitTime.ToString('yyyy-MM-dd HH:mm:ss', $culture) },
        @{ Name = 'LAST_COMPILE_TIME'; Address = '0800F040'; Value = $compileTime.ToString('yyyy-MM-dd HH:mm:ss', $culture) },
        @{ Name = 'LAST_COMPILE_EMAIL'; Address = '0800F060'; Value = $email }
    )
    $lines = @('#include "auto_version.h"', '')
    foreach ($field in $fields) {
        $literal = ConvertTo-VersionLiteral -Name $field.Name -Value $field.Value
        $lines += 'const char {0}[_MAX_LEN_] __attribute__((section(".ARM.__at_0x{1}"), used)) = {2};' -f $field.Name, $field.Address, $literal
    }
    $content = ($lines -join "`n") + "`n"
    $outputPath = [System.IO.Path]::GetFullPath($OutputFile)
    [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($outputPath)) | Out-Null
    if (-not [System.IO.File]::Exists($outputPath) -or
        [System.IO.File]::ReadAllText($outputPath) -cne $content) {
        [System.IO.File]::WriteAllText($outputPath, $content, [System.Text.UTF8Encoding]::new($false))
    }
}
catch {
    [Console]::Error.WriteLine("Version generation failed: $($_.Exception.Message)")
    exit 1
}