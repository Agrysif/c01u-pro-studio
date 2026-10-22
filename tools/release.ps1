<#
.SYNOPSIS
    Выпуск новой версии C01U Pro Studio на GitHub с автообновлением.

.DESCRIPTION
    1. Собирает всё и прогоняет тесты (build.ps1 all).
    2. Кладёт установщик C01UProStudio-<версия>.exe в dist\release.
    3. Пишет update.txt (версия, имя, размер и SHA-256 установщика, что нового) и подписывает его
       секретным ключом разработчика (ECDSA P-256) -> update.txt.sig.
    4. Проверяет подпись тем же кодом, что работает в программе (build\update_test.exe verify).
    5. Публикует выпуск v<версия> через GitHub CLI и проверяет его так, как его увидят
       установленные копии (build\update_test.exe online).

    Перед выпуском поднимите версию в src\shared\version.h и опишите изменения в releases\<версия>\:
        notes.ru.txt, notes.en.txt — коротко, для пульта;
        github.md                  — описание выпуска на GitHub (RU + EN).

    Секретный ключ лежит вне репозитория: %USERPROFILE%\.c01u-pro-studio\signing-key.pem.
    Без него установленные копии не примут обновление — храните резервную копию.

.EXAMPLE
    pwsh .\tools\release.ps1 -DryRun     # всё, кроме публикации
    pwsh .\tools\release.ps1             # выпуск
#>
param(
    [switch]$DryRun,
    [switch]$SkipBuild,
    [string]$KeyPath = (Join-Path $env:USERPROFILE ".c01u-pro-studio\signing-key.pem")
)

$ErrorActionPreference = "Stop"
if ($PSVersionTable.PSVersion.Major -lt 7) {
    # чтение PEM-ключа и подпись в нужном формате (r||s) умеет .NET из PowerShell 7
    throw "Запустите через PowerShell 7: pwsh .\tools\release.ps1 (установка: winget install Microsoft.PowerShell)"
}
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
function Write-Step($t) { Write-Host "`n== $t" -ForegroundColor Cyan }
function Write-Ok($t) { Write-Host "   $t" -ForegroundColor Green }

$root = Split-Path -Parent $PSScriptRoot
$versionH = Get-Content -Raw (Join-Path $root "src\shared\version.h")
$version = [regex]::Match($versionH, '#define MS_VERSION_STR\s+"([0-9.]+)"').Groups[1].Value
$repo = [regex]::Match($versionH, '#define MS_UPDATE_REPO\s+"([^"]+)"').Groups[1].Value
$parts = [regex]::Matches($versionH, '#define MS_VERSION_(MAJOR|MINOR|PATCH)\s+(\d+)') | ForEach-Object { $_.Groups[2].Value }
if (-not $version -or -not $repo) { throw "Не удалось прочитать версию или репозиторий из version.h" }
if (($parts -join ".") -ne $version) { throw "В version.h не совпадают MS_VERSION_STR ($version) и MAJOR.MINOR.PATCH ($($parts -join '.'))" }
$tag = "v$version"
$notesDir = Join-Path $root "releases\$version"
$out = Join-Path $root "dist\release"
$tool = Join-Path $root "build\update_test.exe"

Write-Step "Выпуск $tag в $repo"
foreach ($f in "notes.ru.txt", "notes.en.txt", "github.md") {
    if (-not (Test-Path (Join-Path $notesDir $f))) { throw "Нет файла releases\$version\$f — опишите, что нового" }
}
if (-not (Test-Path $KeyPath)) { throw "Нет секретного ключа подписи: $KeyPath" }

$gh = $null
if (-not $DryRun) {
    $cmd = Get-Command gh -ErrorAction SilentlyContinue
    $gh = if ($cmd) { $cmd.Source } elseif ($env:GH_PATH -and (Test-Path $env:GH_PATH)) { $env:GH_PATH } else { $null }
    if (-not $gh) { throw "Не найден GitHub CLI. Установите (winget install GitHub.cli) или задайте путь в GH_PATH" }
    & $gh release view $tag --repo $repo *> $null
    if ($LASTEXITCODE -eq 0) { throw "Выпуск $tag уже есть на GitHub. Поднимите версию в src\shared\version.h" }
    if (git -C $root status --porcelain) { throw "Есть незакоммиченные изменения — сначала сделайте коммит" }
}

# --- 1. Сборка и тесты
if (-not $SkipBuild) {
    & (Join-Path $root "build.ps1") all  # при ошибке остановится сам
}

# --- 2. Установщик
Write-Step "Установщик"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
$exeName = "C01UProStudio-$version.exe"
$exe = Join-Path $out $exeName
Copy-Item (Join-Path $root "dist\C01UProStudio.exe") $exe
$size = (Get-Item $exe).Length
$sha = (Get-FileHash -Algorithm SHA256 $exe).Hash.ToLowerInvariant()
Write-Ok "$exeName — $([math]::Round($size / 1MB, 2)) МБ, SHA-256 $sha"

# --- 3. Описание версии и подпись
Write-Step "Описание версии и подпись"
function Get-Notes($file) {
    $t = (Get-Content -Raw -Encoding UTF8 (Join-Path $notesDir $file)).Trim()
    return $t.Replace("`r`n", "`n").Replace("`n", "\n")
}
$manifest = @(
    "name=c01u-pro-studio"
    "version=$version"
    "file=$exeName"
    "sha256=$sha"
    "size=$size"
    "page=https://github.com/$repo/releases/tag/$tag"
    "notes_ru=$(Get-Notes 'notes.ru.txt')"
    "notes_en=$(Get-Notes 'notes.en.txt')"
) -join "`n"
$manifest += "`n"
$manifestPath = Join-Path $out "update.txt"
$manifestBytes = [Text.UTF8Encoding]::new($false).GetBytes($manifest)
[IO.File]::WriteAllBytes($manifestPath, $manifestBytes)

$ecdsa = [System.Security.Cryptography.ECDsa]::Create()
try {
    $ecdsa.ImportFromPem((Get-Content -Raw $KeyPath))
    # SignData в .NET отдаёт подпись r||s (64 байта) — тот формат, что ждёт программа
    $sig = $ecdsa.SignData($manifestBytes, [System.Security.Cryptography.HashAlgorithmName]::SHA256)
} finally {
    $ecdsa.Dispose()
}
$sigPath = Join-Path $out "update.txt.sig"
[IO.File]::WriteAllText($sigPath, [Convert]::ToBase64String($sig))

& $tool verify $manifestPath $sigPath
if ($LASTEXITCODE -ne 0) { throw "Подпись не принимается ключом, вшитым в программу! Ключ не тот?" }
Write-Ok "Подпись верна"

$assets = @($exe, $manifestPath, $sigPath)
if ($DryRun) {
    Write-Ok "Проверочный прогон закончен, на GitHub ничего не отправлено. Файлы:"
    $assets | ForEach-Object { Write-Host "    $_" }
    return
}

# --- 4. Публикация
Write-Step "Публикация на GitHub"
git -C $root push
if ($LASTEXITCODE -ne 0) { throw "git push не удался" }
& $gh release create $tag @assets --repo $repo --title "C01U Pro Studio $version" `
    --notes-file (Join-Path $notesDir "github.md") --latest
if ($LASTEXITCODE -ne 0) { throw "Не удалось создать выпуск на GitHub" }

Write-Step "Проверка выпуска так, как его видят установленные копии"
& $tool online $repo
if ($LASTEXITCODE -ne 0) { throw "Установленные копии не смогут обновиться с этого выпуска — проверьте файлы на GitHub" }
Write-Ok "Готово: https://github.com/$repo/releases/tag/$tag"
