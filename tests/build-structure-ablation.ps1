# Compile-only ablation. Never starts the applications or changes display state.
param([switch] $VerifyRelease)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$artifacts = Join-Path $PSScriptRoot 'artifacts/structure-ablation'
$output = Join-Path $root 'build/structure-ablation'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$baseline = [IO.File]::ReadAllText((Join-Path $artifacts 'baseline.cpp')).Replace("`r`n", "`n")
function Replace-One($source, $old, $replacement) {
    $old = $old.Replace("`r`n", "`n")
    $replacement = $replacement.Replace("`r`n", "`n")
    if ([regex]::Matches($source, [regex]::Escape($old)).Count -ne 1) { throw "Source anchor mismatch: $old" }
    $source.Replace($old, $replacement)
}
function Local-Open($source) {
    $declaration = 'ULONG (WINAPI* nativeOpen)(GUID*, ULONG, HANDLE*);'
    $source = Replace-One $source ($declaration + "`n") ''
    Replace-One $source 'void OpenBrightness() {' ("void OpenBrightness() {`n    " + $declaration)
}
$close = @'
void CloseBrightness() {
    nativeClose(brightnessMethodBlock);
    nativeClose(brightnessDataBlock);
    FreeLibrary(brightnessLibrary);
}

'@
function Inline-Close($source) {
    $source = Replace-One $source ($close + "`n") ''
    Replace-One $source '    CloseBrightness();' "    nativeClose(brightnessMethodBlock);`n    nativeClose(brightnessDataBlock);`n    FreeLibrary(brightnessLibrary);"
}
$oldGamma = @'
        for (int channel = 0; channel < 3; ++channel)
            for (int entry = 0; entry < 256; ++entry)
                ramp[channel][entry] = static_cast<WORD>(
                    originalGamma[channel][entry] / (dark ? 2 : 1));
        SetDeviceGammaRamp(display, ramp);
'@
$newGamma = @'
        if (dark)
            for (int channel = 0; channel < 3; ++channel)
                for (int entry = 0; entry < 256; ++entry)
                    ramp[channel][entry] = originalGamma[channel][entry] / 2;
        SetDeviceGammaRamp(display, dark ? ramp : originalGamma);
'@
function Direct-Restore($source) { Replace-One $source $oldGamma $newGamma }
$openMatch = [regex]::Match($baseline, '(?s)void OpenBrightness\(\) \{\n(.*?)\n\}\n\n')
if (!$openMatch.Success) { throw 'OpenBrightness body not found' }
$inlineOpen = Replace-One $baseline $openMatch.Value ''
$inlineOpen = Replace-One $inlineOpen '    OpenBrightness();' $openMatch.Groups[1].Value
$variants = [ordered]@{
    baseline = $baseline
    local_open = (Local-Open $baseline)
    inline_close = (Inline-Close $baseline)
    direct_gamma_restore = (Direct-Restore $baseline)
    inline_open = $inlineOpen
    optimized = (Direct-Restore (Inline-Close (Local-Open $baseline)))
}
if (!$VerifyRelease) {
$commands = @('@echo off', 'call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul')
foreach ($name in $variants.Keys) {
    $source = Join-Path $artifacts ($name + '.cpp')
    if ($name -ne 'baseline') { [IO.File]::WriteAllText($source, $variants[$name], [Text.UTF8Encoding]::new($false)) }
    $prefix = Join-Path $output $name
    $commands += "cl /nologo /std:c++20 /utf-8 /O1 /GL /MT /EHsc /W4 /DUNICODE /D_UNICODE `"$source`" /Fo`"$prefix.obj`" /Fe`"$prefix.exe`" /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /NODEFAULTLIB:libucrt.lib ucrt.lib User32.lib Gdi32.lib Shell32.lib Magnification.lib Imm32.lib"
    $commands += 'if errorlevel 1 exit /b 1'
}
$build = Join-Path $output 'build.cmd'
[IO.File]::WriteAllLines($build, $commands)
& $build > (Join-Path $artifacts 'build.txt') 2>&1
if ($LASTEXITCODE -ne 0) { throw 'Ablation build failed; see build.txt' }
}

# Only COFF/debug timestamps are normalized; executable/data bytes are retained.
function Read-PE($path) {
    $bytes = [IO.File]::ReadAllBytes($path)
    $pe = [BitConverter]::ToInt32($bytes, 0x3c)
    [Array]::Clear($bytes, $pe + 8, 4)
    $optional = $pe + 24
    if ([BitConverter]::ToUInt16($bytes, $optional) -ne 0x20b) { throw 'Expected PE32+' }
    $sectionStart = $optional + [BitConverter]::ToUInt16($bytes, $pe + 20)
    $sections = @()
    for ($i = 0; $i -lt [BitConverter]::ToUInt16($bytes, $pe + 6); ++$i) {
        $s = $sectionStart + 40 * $i
        $sections += [pscustomobject]@{
            name = [Text.Encoding]::ASCII.GetString($bytes, $s, 8).Trim([char]0)
            virtualBytes = [BitConverter]::ToUInt32($bytes, $s + 8)
            rva = [BitConverter]::ToUInt32($bytes, $s + 12)
            rawBytes = [BitConverter]::ToUInt32($bytes, $s + 16)
            rawOffset = [BitConverter]::ToUInt32($bytes, $s + 20)
        }
    }
    $debugRva = [BitConverter]::ToUInt32($bytes, $optional + 112 + 6 * 8)
    $debugBytes = [BitConverter]::ToUInt32($bytes, $optional + 112 + 6 * 8 + 4)
    foreach ($s in $sections) {
        if ($debugRva -ge $s.rva -and $debugRva -lt $s.rva + $s.rawBytes) {
            $debug = $s.rawOffset + $debugRva - $s.rva
            for ($i = 0; $i -lt $debugBytes; $i += 28) { [Array]::Clear($bytes, $debug + $i + 4, 4) }
        }
    }
    $hasher = [Security.Cryptography.SHA256]::Create()
    $hash = [BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-', '')
    $hasher.Dispose()
    [pscustomobject]@{
        exeBytes = $bytes.Length
        normalizedSha256 = $hash
        imageBytes = [BitConverter]::ToUInt32($bytes, $optional + 56)
        sections = $sections
    }
}
$results = @()
foreach ($name in $variants.Keys) {
    $pe = Read-PE (Join-Path $output ($name + '.exe'))
    if ($name -eq 'baseline') { $baselineHash = $pe.normalizedSha256 }
    $results += [pscustomobject]@{
        variant = $name
        sourceSha256 = (Get-FileHash (Join-Path $artifacts ($name + '.cpp'))).Hash
        sourceLines = $variants[$name].TrimEnd().Split("`n").Length
        identicalToBaselineExceptTimestamps = ($pe.normalizedSha256 -eq $baselineHash)
        pe = $pe
    }
}
$results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $artifacts 'results.json') -Encoding UTF8
$results | Select-Object variant,sourceLines,identicalToBaselineExceptTimestamps,@{n='exeBytes';e={$_.pe.exeBytes}},@{n='imageBytes';e={$_.pe.imageBytes}} | Format-Table -AutoSize
if ($VerifyRelease) {
    $release = Read-PE (Join-Path $root 'dist/ScreenSave.exe')
    $optimized = $results | Where-Object variant -eq 'optimized'
    $same = $release.normalizedSha256 -eq $optimized.pe.normalizedSha256
    [pscustomobject]@{
        identicalToOptimizedExceptTimestamps = $same
        normalizedSha256 = $release.normalizedSha256
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifacts 'release-verification.json') -Encoding UTF8
    if (!$same) { throw 'Release differs from the optimized ablation candidate' }
}
