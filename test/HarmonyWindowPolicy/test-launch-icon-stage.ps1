param(
    [string]$ProjectDirectory = 'tmp/build-v5.1.5-harmonyos-release-arm64/harmonyos-build'
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$stagedRoot = [IO.Path]::GetFullPath($ProjectDirectory, $projectRoot)
$app = (Get-Content -LiteralPath (Join-Path $stagedRoot 'AppScope/app.json5') -Raw | ConvertFrom-Json).app
if ($app.versionName -cne '5.1.5') { throw 'The staged application must declare version 5.1.5.' }
$module = (Get-Content -LiteralPath (Join-Path $stagedRoot 'entry/src/main/module.json5') -Raw | ConvertFrom-Json).module
$ability = @($module.abilities | Where-Object { $_.name -eq 'QAbility' })
if ($ability.Count -ne 1 -or $ability[0].startWindowIcon -cne $ability[0].icon) {
    throw 'The startup window must use the QGC ability icon.'
}
$icon = [regex]::Match($ability[0].icon, '^\$media:([A-Za-z0-9_]+)$')
if (-not $icon.Success) { throw 'The QGC icon must reference a media resource.' }
$mediaPath = Join-Path $stagedRoot ('entry/src/main/resources/base/media/' + $icon.Groups[1].Value + '.png')
$sourcePath = Join-Path $projectRoot 'resources/icons/android_512x512.png'
if ((Get-FileHash -LiteralPath $mediaPath).Hash -cne (Get-FileHash -LiteralPath $sourcePath).Hash) {
    throw 'The staged startup icon must match the official QGC icon.'
}
if ($ability[0].startWindow -cne '$profile:qgc_start_window') { throw 'The system loading-window profile must be staged.' }
$profile = Get-Content -LiteralPath (Join-Path $stagedRoot 'entry/src/main/resources/base/profile/qgc_start_window.json') -Raw | ConvertFrom-Json
if ($profile.startWindowType -cne 'REQUIRED_SHOW' -or $profile.startWindowAppIcon -cne $ability[0].icon -or
    $profile.startWindowBackgroundColor -cne $ability[0].startWindowBackground) { throw 'The enhanced starting window must retain official icon/background resources.' }
$background = [regex]::Match($profile.startWindowBackgroundColor, '^\$color:([A-Za-z0-9_]+)$')
$colors = (Get-Content -LiteralPath (Join-Path $stagedRoot 'entry/src/main/resources/base/element/color.json') -Raw | ConvertFrom-Json).color
if (-not $background.Success -or @($colors | Where-Object { $_.name -ceq $background.Groups[1].Value }).Count -ne 1) {
    throw 'The required starting-window background must resolve to a color resource.'
}
if (($colors | Where-Object { $_.name -ceq $background.Groups[1].Value }).value -cne '#222222') {
    throw 'The loading window must use the QGC dark window background.'
}
Write-Output 'PASS startup window references the existing official QGC icon'
Write-Output 'PASS visible loading-window profile retains a QGC background and official icon fallback'
