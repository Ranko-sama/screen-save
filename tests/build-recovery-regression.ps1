# No display tests: builds variants and runs only the in-memory API recorder.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'build/recovery-fix'
$artifacts = Join-Path $PSScriptRoot 'artifacts/recovery-fix'
New-Item -ItemType Directory -Force -Path $output | Out-Null
function Replace-One($source, $old, $replacement) {
    $old = $old.Replace("`r`n", "`n")
    $replacement = $replacement.Replace("`r`n", "`n")
    if ([regex]::Matches($source, [regex]::Escape($old)).Count -ne 1) { throw "Source anchor mismatch: $old" }
    $source.Replace($old, $replacement)
}
$baseline = [IO.File]::ReadAllText((Join-Path $artifacts 'baseline.cpp')).Replace("`r`n", "`n")
$fixed = [IO.File]::ReadAllText((Join-Path $artifacts 'session_only.cpp')).Replace("`r`n", "`n")
$oldCursor = @'
            // Refresh the cursor at its current physical pixel, without DPI rounding.
            POINT cursor;
            GetPhysicalCursorPos(&cursor);
            SetPhysicalCursorPos(cursor.x, cursor.y);
'@
$newCursor = @'
            // Relative zero movement cannot write back a stale cursor position.
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE;
            SendInput(1, &input, sizeof(input));
'@
$candidate = Replace-One $fixed $oldCursor $newCursor
$session = @'
    if (message == WM_ENDSESSION) {
        // Windows may terminate us as soon as this callback returns.
        if (w && isDark) ToggleBlack();
        return 0;
    }
'@
$extraQuit = @'
        if (w) {
            if (isDark) ToggleBlack();
            PostQuitMessage(0);
        }
'@
$variants = [ordered]@{
    baseline = $baseline
    session_only = $fixed
    cursor_candidate = $candidate
    no_session = (Replace-One $candidate $session '')
    no_confirmation = (Replace-One $candidate 'if (w && isDark) ToggleBlack();' 'if (isDark) ToggleBlack();')
    no_dark_guard = (Replace-One $candidate 'if (w && isDark) ToggleBlack();' 'if (w) ToggleBlack();')
    extra_quit = (Replace-One $candidate '        if (w && isDark) ToggleBlack();' $extraQuit)
}
$commands = @('@echo off', 'call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul')
$libs = 'ucrt.lib User32.lib Gdi32.lib Shell32.lib Magnification.lib Imm32.lib'
foreach ($name in $variants.Keys) {
    $sourcePath = Join-Path $artifacts ($name + '.cpp')
    if ($name -ne 'baseline') { [IO.File]::WriteAllText($sourcePath, $variants[$name], [Text.UTF8Encoding]::new($false)) }
    $probePath = Join-Path $output ($name + '-probe.cpp')
    $probe = '#define SCREEN_SAVE_SOURCE "' + $sourcePath.Replace('\', '/') + '"' + "`n" +
        '#include "' + (Join-Path $PSScriptRoot 'recovery-model-probe.cpp').Replace('\', '/') + '"' + "`n"
    [IO.File]::WriteAllText($probePath, $probe, [Text.UTF8Encoding]::new($false))
    $prefix = Join-Path $output ($name + '-probe')
    $commands += "cl /nologo /std:c++20 /utf-8 /O1 /GL /MT /EHsc /W4 /DUNICODE /D_UNICODE `"$probePath`" /Fo`"$prefix.obj`" /Fe`"$prefix.exe`" /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:CONSOLE /NODEFAULTLIB:libucrt.lib $libs"
    $commands += 'if errorlevel 1 exit /b 1'
}
$appSource = Join-Path $artifacts 'cursor_candidate.cpp'
$app = Join-Path $output 'cursor_candidate'
$commands += "cl /nologo /std:c++20 /utf-8 /O1 /GL /MT /EHsc /W4 /DUNICODE /D_UNICODE `"$appSource`" /Fo`"$app.obj`" /Fe`"$app.exe`" /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /NODEFAULTLIB:libucrt.lib $libs"
$commands += 'if errorlevel 1 exit /b 1'
$build = Join-Path $output 'build-regression.cmd'
[IO.File]::WriteAllLines($build, $commands)
& $build > (Join-Path $artifacts 'build-regression.txt') 2>&1
if ($LASTEXITCODE -ne 0) { throw 'Regression build failed; see build-regression.txt' }
# The only executables launched here replace all display/input/power write APIs.
$expectedFailures = @{
    baseline = @{confirmed_session_restores_before_return=4;next_session_uses_fresh_external_snapshot=2;concurrent_move_is_not_rolled_back=1}
    session_only = @{concurrent_move_is_not_rolled_back=1}
    no_session = @{confirmed_session_restores_before_return=4;next_session_uses_fresh_external_snapshot=2}
    no_confirmation = @{cancelled_session_changes_nothing=4;dark_full_state=4;toggle_restores_after_cancel_and_handoffs=2;next_session_uses_fresh_external_snapshot=2}
    no_dark_guard = @{confirmed_session_restores_before_return=4;restore_reuses_session_snapshot=4;normal_session_does_not_write_display=4}
}
$results = foreach ($name in $variants.Keys) {
    $log = @(& (Join-Path $output ($name + '-probe.exe')))
    $code = $LASTEXITCODE
    $log | Set-Content -LiteralPath (Join-Path $artifacts ($name + '-model.txt')) -Encoding UTF8
    $expectedPass = $name -in @('cursor_candidate', 'extra_quit')
    $summary = [regex]::Match($log[-1], '^CHECKS=(\d+) FAILURES=(\d+)$')
    $failed = @($log | Where-Object { $_.StartsWith('FAIL ') })
    $valid = $summary.Success -and [int]$summary.Groups[1].Value -gt 0 -and [int]$summary.Groups[2].Value -eq $failed.Count
    if ($expectedPass) {
        $valid = $valid -and $code -eq 0 -and $failed.Count -eq 0
    } else {
        $expected = $expectedFailures[$name]
        $total = 0
        foreach ($failure in $expected.Keys) {
            $total += $expected[$failure]
            $valid = $valid -and @($failed | Where-Object { $_ -ceq ('FAIL ' + $failure) }).Count -eq $expected[$failure]
        }
        $valid = $valid -and $code -eq 1 -and $failed.Count -eq $total
    }
    [pscustomobject]@{variant=$name;exitCode=$code;expectedPass=$expectedPass;asExpected=$valid;summary=$log[-1]}
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifacts 'model-results.json') -Encoding UTF8
$results | Format-Table -AutoSize
if ($results.asExpected -contains $false) { throw 'Unexpected regression result' }
