# Builds main_en.cpp from main.cpp.
# Every Korean wide string literal is replaced using strings_en.tsv (one "korean|||english" per line).
# Leading/trailing spaces must match the source exactly (they are used for alignment).
# Fails if any Korean literal has no translation.
# Keep this file ASCII only: Windows PowerShell 5.1 reads BOM-less scripts in the ANSI code page.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$utf8 = New-Object Text.UTF8Encoding($false)
$src = [IO.File]::ReadAllText((Join-Path $root 'main.cpp'), [Text.Encoding]::UTF8)

$map = @{}
$lines = [IO.File]::ReadAllLines((Join-Path $root 'strings_en.tsv'), [Text.Encoding]::UTF8)
foreach ($ln in $lines) {
    if ($ln.Length -eq 0) { continue }
    $t = $ln.IndexOf('|||')
    if ($t -lt 0) { Write-Output "BAD LINE (no separator): $ln"; exit 1 }
    $ko = $ln.Substring(0, $t)
    $en = $ln.Substring($t + 3)
    # Editors strip trailing spaces from the line, so copy the Korean side's trailing spaces onto the English side.
    $koTrail = $ko.Length - $ko.TrimEnd(' ').Length
    $en = $en.TrimEnd(' ') + (' ' * $koTrail)
    $map[$ko] = $en
}

$hangul = '[' + [char]0xAC00 + '-' + [char]0xD7A3 + [char]0x3131 + '-' + [char]0x318E + ']'
$litPattern = 'L"((?:[^"\\\r\n]|\\.)*)"'

$script:missing = New-Object System.Collections.Generic.List[string]
$script:used = @{}
$evaluator = {
    param($m)
    $v = $m.Groups[1].Value
    if ($v -notmatch $hangul) { return $m.Value }
    if ($map.ContainsKey($v)) { $script:used[$v] = 1; return 'L"' + $map[$v] + '"' }
    $script:missing.Add($v)
    return $m.Value
}
$out = [regex]::Replace($src, $litPattern, [Text.RegularExpressions.MatchEvaluator]$evaluator)
$out = $out.Replace('HANGUL_CHARSET', 'DEFAULT_CHARSET')

$left = @([regex]::Matches($out, $litPattern) | Where-Object { $_.Groups[1].Value -match $hangul })
$badEn = @($map.Values | Where-Object { $_ -match $hangul })
Write-Output ("translations: {0}, used: {1}, missing: {2}, korean left in literals: {3}, korean in english side: {4}" -f $map.Count, $script:used.Count, $script:missing.Count, $left.Count, $badEn.Count)
if ($script:missing.Count -gt 0 -or $left.Count -gt 0 -or $badEn.Count -gt 0) {
    $script:missing | Select-Object -Unique | ForEach-Object { Write-Output "MISSING: [$_]" }
    $badEn | ForEach-Object { Write-Output "KOREAN IN ENGLISH: [$_]" }
    exit 1
}
foreach ($k in $map.Keys) { if (-not $script:used.ContainsKey($k)) { Write-Output "UNUSED: [$k]" } }
[IO.File]::WriteAllText((Join-Path $root 'main_en.cpp'), $out, $utf8)
Write-Output 'MAKE_EN_OK'
