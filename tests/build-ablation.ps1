# Developer-only candidate builder. It never launches a candidate or changes display state.
param([string[]] $Names)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $projectRoot 'build\review\ablation'
$baselinePath = Join-Path $PSScriptRoot 'artifacts\ablation\baseline.cpp'
$optimizedPath = Join-Path $PSScriptRoot 'artifacts\ablation\optimized.cpp'
$baseline = [IO.File]::ReadAllText($baselinePath).Replace("`r`n", "`n")
$optimized = [IO.File]::ReadAllText($optimizedPath).Replace("`r`n", "`n")
$candidates = [Collections.Generic.List[object]]::new()

function Replace-Once([string] $source, [string] $old, [string] $replacement) {
    $old = $old.Replace("`r`n", "`n")
    $replacement = $replacement.Replace("`r`n", "`n")
    $count = [regex]::Matches($source, [regex]::Escape($old)).Count
    if ($count -ne 1) {
        throw "Expected one source anchor, found ${count}: $old"
    }
    return $source.Replace($old, $replacement)
}

function Add-Candidate(
    [string] $name, [string] $base, [string] $test,
    [object] $expectedFailure, [string] $description, [string] $source
) {
    $candidates.Add([pscustomobject]@{
        name = $name
        base = $base
        test = $test
        expectedFailure = $expectedFailure
        description = $description
        source = Join-Path $outputDirectory ($name + '.cpp')
        executable = Join-Path $outputDirectory ($name + '.exe')
        content = $source
    })
}

$baselineMode = @'
void SelectMode(DimmingMode mode) {
    if (mode == dimmingMode) return;
    ApplyOutput(false);
    if (isDark) SetBrightness(mode == DimmingMode::Gamma ? 0 : brightnessBeforeDark);
    dimmingMode = mode;
    ApplyDimming();
}
'@
$inactiveMode = @'
void SelectMode(DimmingMode mode) {
    if (mode == dimmingMode) return;
    if (isDark) {
        ApplyOutput(false);
        SetBrightness(mode == DimmingMode::Gamma ? 0 : brightnessBeforeDark);
    }
    dimmingMode = mode;
    if (isDark) ApplyDimming();
}
'@
$modeWithoutPower = @'
void SelectMode(DimmingMode mode) {
    if (mode == dimmingMode) return;
    ApplyOutput(false);
    if (isDark) SetBrightness(mode == DimmingMode::Gamma ? 0 : brightnessBeforeDark);
    dimmingMode = mode;
    ApplyOutput(isDark);
}
'@
$baselineMatrix = @'
        const float gain = dark ? 0.0f : 1.0f;
        for (int row = 0; row < 5; ++row)
            for (int column = 0; column < 3; ++column)
                colors.transform[row][column] *= gain;
'@
$booleanMatrix = @'
        if (dark)
            for (int row = 0; row < 5; ++row)
                for (int column = 0; column < 3; ++column)
                    colors.transform[row][column] = 0.0f;
'@
$wrapper = @'
void ApplyDimming() {
    ApplyOutput(isDark);
    SetThreadExecutionState(ES_CONTINUOUS |
        (isDark ? ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED : 0));
}
'@
$wrapperBody = @'
    ApplyOutput(isDark);
    SetThreadExecutionState(ES_CONTINUOUS |
        (isDark ? ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED : 0));
'@

Add-Candidate 'baseline_no_translate' 'baseline' 'toggle' $false `
    'Remove character translation from the hidden-window message loop.' `
    (Replace-Once $baseline "        TranslateMessage(&message);`n" '')
Add-Candidate 'baseline_no_inactive_writes' 'baseline' 'toggle' $false `
    'Selecting a mode while inactive changes only the selected mode.' `
    (Replace-Once $baseline $baselineMode $inactiveMode)
$withoutModePower = Replace-Once $baseline $baselineMode $modeWithoutPower
Add-Candidate 'baseline_no_mode_power' 'baseline' 'toggle' $false `
    'Do not repeat the continuous power request when switching modes.' $withoutModePower
$localTray = Replace-Once $baseline "NOTIFYICONDATAW tray{};`n" ''
$localTray = Replace-Once $localTray '    tray.cbSize = sizeof(tray);' `
    "    NOTIFYICONDATAW tray{};`n    tray.cbSize = sizeof(tray);"
Add-Candidate 'baseline_local_tray' 'baseline' 'toggle' $false `
    'Keep tray data local to its only owning function.' $localTray
Add-Candidate 'baseline_matrix_bool' 'baseline' 'toggle' $false `
    'Replace the obsolete numeric matrix gain with black-or-original output.' `
    (Replace-Once $baseline $baselineMatrix $booleanMatrix)
$withoutWrapper = Replace-Once $withoutModePower ($wrapper + "`n`n") ''
$withoutWrapper = Replace-Once $withoutWrapper '    ApplyDimming();' $wrapperBody
Add-Candidate 'baseline_no_wrapper' 'baseline_no_mode_power' 'toggle' $false `
    'After removing repeated mode power requests, inline the one remaining wrapper call.' $withoutWrapper

$blanket = @'
    CoSetProxyBlanket(wmi, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
'@
Add-Candidate 'optimized_no_com_blanket' 'optimized' 'toggle' $null `
    'Experiment: determine whether the explicit local WMI proxy blanket is necessary on this machine.' `
    (Replace-Once $optimized ($blanket + "`n") '')

Add-Candidate 'no_old_output_restore' 'optimized' 'toggle' $true `
    'Negative control: leave the previous mode active during a mode switch.' `
    (Replace-Once $optimized "        ApplyOutput(dimmingMode, false);`n" '')
Add-Candidate 'no_matrix_zero' 'optimized' 'toggle' $true `
    'Negative control: matrix mode no longer writes black RGB output.' `
    (Replace-Once $optimized ($booleanMatrix + "`n") '')
Add-Candidate 'no_gamma_gain' 'optimized' 'toggle' $true `
    'Negative control: Gamma mode writes the original ramp without dimming.' `
    (Replace-Once $optimized 'originalGamma[channel][entry] / (dark ? 2 : 1)' `
        'originalGamma[channel][entry]')
$hardwareFunction = @'
void SetBrightness(int brightness) {
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_UI1;
    value.bVal = static_cast<BYTE>(brightness);
    brightnessArguments->Put(L"Brightness", 0, &value, 0);
    BSTR method = SysAllocString(L"WmiSetBrightness");
    wmi->ExecMethod(brightnessMethodPath, method,
        0, nullptr, brightnessArguments, nullptr, nullptr);
    SysFreeString(method);
}
'@
Add-Candidate 'no_hardware_write' 'optimized' 'toggle' $true `
    'Negative control: hardware brightness writes do nothing.' `
    (Replace-Once $optimized $hardwareFunction 'void SetBrightness(int) {}')
Add-Candidate 'no_brightness_snapshot' 'optimized' 'toggle' $true `
    'Negative control: discard the hardware brightness restore snapshot.' `
    (Replace-Once $optimized "        brightnessBeforeDark = ReadBrightness();`n" '')
Add-Candidate 'no_matrix_snapshot' 'optimized' 'toggle' $true `
    'Negative control: discard the original matrix snapshot.' `
    (Replace-Once $optimized "        MagGetFullscreenColorEffect(&originalColors);`n" '')
Add-Candidate 'no_gamma_snapshot' 'optimized' 'toggle' $true `
    'Negative control: discard the original Gamma ramp snapshot.' `
    (Replace-Once $optimized "        GetDeviceGammaRamp(display, originalGamma);`n" '')
Add-Candidate 'no_exit_restore' 'optimized' 'toggle' $true `
    'Negative control: exit an active dark session without restoring its state.' `
    (Replace-Once $optimized "    if (isDark) ToggleBlack();`n" '')
$powerRequest = @'
    SetThreadExecutionState(ES_CONTINUOUS |
        (isDark ? ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED : 0));
'@
Add-Candidate 'no_keepalive' 'optimized' 'toggle' $true `
    'Negative control: never request display or system keep-awake.' `
    (Replace-Once $optimized ($powerRequest + "`n") '')
Add-Candidate 'no_continuous' 'optimized' 'toggle' $true `
    'Negative control: issue only an idle-timer reset, not a persistent keep-awake request.' `
    (Replace-Once $optimized $powerRequest `
        '    SetThreadExecutionState(isDark ? ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED : 0);')
Add-Candidate 'no_display_request' 'optimized' 'toggle' $true `
    'Negative control: omit the display keep-awake requirement.' `
    (Replace-Once $optimized 'isDark ? ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED : 0' `
        'isDark ? ES_SYSTEM_REQUIRED : 0')
Add-Candidate 'no_system_request' 'optimized' 'toggle' $true `
    'Negative control: omit the system keep-awake requirement.' `
    (Replace-Once $optimized 'isDark ? ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED : 0' `
        'isDark ? ES_DISPLAY_REQUIRED : 0')
Add-Candidate 'no_cursor_visibility' 'optimized' 'cursor' $true `
    'Negative control: omit hiding and showing the system cursor.' `
    (Replace-Once $optimized "        MagShowSystemCursor(!dark);`n" '')
$cursorRefresh = @'
        if (!dark) {
            // Refresh the cursor at its current physical pixel, without DPI rounding.
            POINT cursor;
            GetPhysicalCursorPos(&cursor);
            SetPhysicalCursorPos(cursor.x, cursor.y);
        }
'@
Add-Candidate 'no_cursor_refresh' 'optimized' 'cursor' $true `
    'Negative control: omit the physical-position refresh required for stationary cursor restoration.' `
    (Replace-Once $optimized ($cursorRefresh + "`n") '')
$ownerCheck = @'
    if (!RegisterHotKey(window, 3, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'B')) {
        DestroyWindow(window);
        return 0;
    }
'@
Add-Candidate 'no_single_owner' 'optimized' 'review' $true `
    'Negative control: continue running when another instance already owns the hotkey and display session.' `
    (Replace-Once $optimized $ownerCheck `
        "    RegisterHotKey(window, 3, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'B');")
$safeHandoff = @'
        ApplyOutput(mode, true);
        SetBrightness(mode == DimmingMode::Gamma ? 0 : brightnessBeforeDark);
        ApplyOutput(dimmingMode, false);
'@
$oldHandoff = @'
        ApplyOutput(dimmingMode, false);
        SetBrightness(mode == DimmingMode::Gamma ? 0 : brightnessBeforeDark);
        ApplyOutput(mode, true);
'@
Add-Candidate 'old_handoff_order' 'optimized' 'review' $true `
    'Negative control: remove the old effect before enabling the new effect, exposing a bright interval.' `
    (Replace-Once $optimized $safeHandoff $oldHandoff)

Add-Candidate 'no_focus_guard' 'optimized' 'menu' $true `
    'Negative control: restoring the previous foreground unconditionally steals a newly clicked window focus.' `
    (Replace-Once $optimized 'if (GetForegroundWindow() == window) SetForegroundWindow(foreground);' `
        'SetForegroundWindow(foreground);')

# Validate every mutation before writing any candidate. All files stay under build/.
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$selected = @($candidates | Where-Object { !$Names -or $_.name -in $Names })
foreach ($candidate in $selected) {
    [IO.File]::WriteAllText($candidate.source, $candidate.content, [Text.UTF8Encoding]::new($false))
}

$vsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
Import-Module (Join-Path $vsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
foreach ($candidate in $selected) {
    $objectPath = Join-Path $outputDirectory ($candidate.name + '.obj')
    $buildLog = Join-Path $outputDirectory ($candidate.name + '.build.txt')
    & cl /nologo /std:c++20 /utf-8 /O1 /GL /MT /EHsc /W4 /DUNICODE /D_UNICODE `
        $candidate.source "/Fo$objectPath" "/Fe$($candidate.executable)" `
        /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS `
        User32.lib Gdi32.lib Shell32.lib Magnification.lib Ole32.lib OleAut32.lib WbemUuid.lib `
        2>&1 | Tee-Object -FilePath $buildLog
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $($candidate.name)" }
}
$manifest = @($candidates | Select-Object name, base, test, expectedFailure, description, source, executable)
$manifestPath = Join-Path $outputDirectory 'manifest.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 3) + "`n", [Text.UTF8Encoding]::new($false))
Write-Output "Built $($selected.Count) candidates. No candidates were launched. Manifest: $manifestPath"
