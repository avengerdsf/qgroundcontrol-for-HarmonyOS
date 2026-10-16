param(
    [ValidateSet('x86_64', 'arm64')]
    [string]$Architecture = 'x86_64',
    [ValidateSet('Stage', 'Build', 'Run')]
    [string]$Action = 'Build',
    [ValidateSet('debug', 'release')]
    [string]$BuildMode = 'debug',
    [ValidateRange(1, 2147483647)]
    [long]$VersionCode = 5000800,
    [string]$BuildDirectory,
    [ValidateLength(7, 128)]
    [ValidatePattern('^[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z][A-Za-z0-9_]*){2,}$')]
    [string]$BundleName = 'org.mavlink.qgroundcontrol',
    [string]$Device,
    [string]$DevEcoCli = 'C:/Users/chen/AppData/Roaming/npm/devecocli.ps1',
    [string]$DevEcoPath = 'D:/Progrem/Huawei/DevEco Studio'
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = "tmp/build-v5.0.8-harmonyos-$Architecture"
}
$buildRoot = [IO.Path]::GetFullPath($BuildDirectory, $projectRoot)
$tmpRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp'))
if (-not $buildRoot.StartsWith($tmpRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The build directory must be inside the project tmp directory.'
}
$settingsPath = Join-Path $buildRoot 'QGroundControl-harmony-deployment-settings.json'
$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
$packageRoot = Join-Path $buildRoot 'harmonyos-build'
$harmonyDeployQt = Join-Path $projectRoot 'tmp/toolchains/Qt/6.12.0/mingw_64/bin/harmonydeployqt.exe'
$hvigorDependenciesPattern = '^' + [regex]::Escape((Join-Path $tmpRoot 'cache/hvigor/project_caches')) + '\\[^\\]+\\workspace\\node_modules$'

function Assert-NoReparse([string]$Path, [switch]$Tree) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    if ($fullPath -ne $tmpRoot -and -not $fullPath.StartsWith($tmpRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The packaging path must be inside the project tmp directory.'
    }
    $currentPath = $tmpRoot
    $relativePath = $fullPath.Substring($tmpRoot.Length).TrimStart('\')
    foreach ($part in @('') + @($relativePath.Split('\') | Where-Object { $_ })) {
        if ($part) { $currentPath = Join-Path $currentPath $part }
        try {
            $item = Get-Item -LiteralPath $currentPath -Force -ErrorAction Stop
        } catch [System.Management.Automation.ItemNotFoundException] {
            $item = $null
        }
        if ($item) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "The packaging path must not traverse a junction or symbolic link: $currentPath"
            }
        }
    }
    if ($Tree -and (Test-Path -LiteralPath $fullPath -PathType Container)) {
        $pending = [Collections.Generic.Stack[string]]::new()
        $pending.Push($fullPath)
        while ($pending.Count) {
            foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
                if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    throw "The packaging write tree contains a junction or symbolic link: $($item.FullName)"
                }
                if ($item.PSIsContainer -and $item.FullName -notmatch $hvigorDependenciesPattern) {
                    $pending.Push($item.FullName)
                }
            }
        }
    }
}

function Get-QgcSigningSnapshot([string]$ProfilePath) {
    Assert-NoReparse $ProfilePath
    if (-not (Test-Path -LiteralPath $ProfilePath -PathType Leaf)) { return $null }
    try {
        $profile = Get-Content -LiteralPath $ProfilePath -Raw | ConvertFrom-Json
    } catch {
        throw 'Unable to read the existing build profile signing configuration.'
    }
    if (-not $profile.app.signingConfigs) { return $null }
    return [pscustomobject]@{
        signingConfigs = @($profile.app.signingConfigs)
        products = @($profile.app.products | Where-Object { $_.PSObject.Properties['signingConfig'] } | ForEach-Object {
            [pscustomobject]@{ name = $_.name; signingConfig = $_.signingConfig }
        })
    }
}

function Restore-QgcSigningSnapshot([string]$ProfilePath, $Snapshot) {
    if ($null -eq $Snapshot) { return }
    Assert-NoReparse $ProfilePath
    try {
        $profile = Get-Content -LiteralPath $ProfilePath -Raw | ConvertFrom-Json
    } catch {
        throw 'Unable to read the staged build profile for signing restoration.'
    }
    if (-not $profile.app) { throw 'The staged build profile must contain app configuration.' }
    $profile.app | Add-Member -NotePropertyName signingConfigs -NotePropertyValue @($Snapshot.signingConfigs) -Force
    foreach ($product in $profile.app.products) {
        $savedProduct = @($Snapshot.products | Where-Object { $_.name -ceq $product.name })
        if ($savedProduct.Count -gt 1) { throw 'Signing restoration requires unique product names.' }
        if ($savedProduct.Count -eq 1) {
            $product | Add-Member -NotePropertyName signingConfig -NotePropertyValue $savedProduct[0].signingConfig -Force
        }
    }
    Set-Content -LiteralPath $ProfilePath -Value ($profile | ConvertTo-Json -Depth 32) -Encoding utf8 -NoNewline
}

function Set-QgcMainPageBackPress([string]$PagePath) {
    Assert-NoReparse $PagePath
    $pageText = (Get-Content -LiteralPath $PagePath -Raw).Replace("`r`n", "`n")
    $anchor = '  aboutToAppear(): void {'
    $header = "@Entry`n@Component`nstruct WindowNativeNode {`n"
    if ([regex]::Matches($pageText, [regex]::Escape($header)).Count -ne 1 -or
            [regex]::Matches($pageText, [regex]::Escape($anchor)).Count -ne 1 -or
            [regex]::Matches($pageText, [regex]::Escape('XComponent({')).Count -ne 1 -or
            [regex]::Matches($pageText, [regex]::Escape('.onAttach(this.createInfo.onAttach)')).Count -ne 1) {
        throw 'The Qt main page must retain one recognized Entry component, lifecycle and native surface.'
    }
    $handler = "  onBackPress(): boolean {`n    console.info('QGC main page: system Back consumed');`n    return true;`n  }`n`n"
    if ($pageText -match '\bonBackPress\b') {
        if ([regex]::Matches($pageText, '\bonBackPress\b').Count -ne 1 -or
                [regex]::Matches($pageText, [regex]::Escape($handler + $anchor)).Count -ne 1) {
            throw 'The Qt main page has an unknown, duplicate or partial Back handler.'
        }
        return
    }
    $pageText = $pageText.Replace($anchor, $handler + $anchor)
    Set-Content -LiteralPath $PagePath -Value $pageText -Encoding utf8 -NoNewline
}

function Set-QgcWindowTitleBar([string]$PagePath) {
    Assert-NoReparse $PagePath
    $pageText = (Get-Content -LiteralPath $PagePath -Raw).Replace("`r`n", "`n")
    $hooks = @(
        @{
            Source = 'interface WindowNativeNodeCreateInfo {'
            Target = "import { window } from '@kit.ArkUI';`nimport { common } from '@kit.AbilityKit';`nimport { QGCWindowTitleBar, QGCTitleBarController, QGCTitleBarState } from '../qability/QGCWindowTitleBar';`n`ninterface WindowNativeNodeCreateInfo {"
        },
        @{
            Source = "  @State private createInfo: WindowNativeNodeCreateInfo | undefined = undefined"
            Target = @'
  @State private createInfo: WindowNativeNodeCreateInfo | undefined = undefined
  @State private titleBarState: QGCTitleBarState = new QGCTitleBarState();
  private readonly titleBarController: QGCTitleBarController = new QGCTitleBarController();
  private titleBarStarted: boolean = false;

  private startTitleBar(): void {
    if (this.titleBarStarted || !this.createInfo) { return; }
    const uiContext = this.getUIContext();
    const windowName: string | undefined = uiContext.getWindowName();
    const context = uiContext.getHostContext() as common.UIAbilityContext | undefined;
    console.info(`QGCWindowTitleBar page node=${this.createInfo.xComponentId} name=${windowName} context=${!!context}`);
    if (!windowName || !context) { return; }
    try {
      const nativeWindow: window.Window = window.findWindow(windowName);
      const mainWindowId: number = context.windowStage.getMainWindowSync().getWindowProperties().id;
      if (uiContext.getWindowId() !== mainWindowId || nativeWindow.getWindowProperties().id !== mainWindowId) {
        return;
      }
      this.titleBarStarted = true;
      this.titleBarController.start(nativeWindow, context, (state: QGCTitleBarState): void => {
        this.titleBarState = state;
      }).catch((error: Error): void => { console.warn(error.message); });
    } catch (caught) {
      console.warn((caught as Error).message);
    }
  }

  private refreshTitleBarLayout(): void {
    this.startTitleBar();
    if (this.titleBarStarted) { this.titleBarController.layoutChanged(); }
  }
'@
        },
        @{
            Source = "    Stack() {`n      if (this.createInfo !== undefined) {"
            Target = "    Column() {`n      QGCWindowTitleBar({ state: this.titleBarState, controller: this.titleBarController })`n      if (this.createInfo !== undefined) {"
        },
        @{
            Source = "        .width('100%')`n        .height('100%')"
            Target = "        .width('100%')`n        .layoutWeight(1)"
        },
        @{
            Source = "    .width('100%')`n    .height('100%')"
            Target = "    .width('100%')`n    .height('100%')`n    .onAppear((): void => { this.startTitleBar(); })`n    .onAreaChange((): void => { this.refreshTitleBarLayout(); })`n    .onDisAppear((): void => { this.titleBarStarted = false; this.titleBarController.destroy(); })"
        }
    )
    $staged = $pageText.Contains('QGCWindowTitleBar')
    foreach ($hook in $hooks) {
        $anchor = if ($staged) { $hook.Target } else { $hook.Source }
        if ([regex]::Matches($pageText, [regex]::Escape($anchor)).Count -ne 1) {
            throw 'The Qt main native-node page must contain one complete title bar integration anchor.'
        }
    }
    if (-not $staged) {
        foreach ($hook in $hooks) { $pageText = $pageText.Replace($hook.Source, $hook.Target) }
        Set-Content -LiteralPath $PagePath -Value $pageText -Encoding utf8 -NoNewline
    }
}

function Initialize-QgcMainPage([string]$PagePath, [string]$TemplateRoot) {
    Assert-NoReparse $PagePath
    if ([string]::IsNullOrWhiteSpace($TemplateRoot)) {
        throw 'Qt deployment settings must provide the HarmonyOS package source directory.'
    }
    $templatePath = Join-Path $TemplateRoot 'entry/src/main/ets/pages/MainWindowNativeNode.ets'
    if (-not (Test-Path -LiteralPath $templatePath -PathType Leaf)) {
        throw "The clean Qt main native-node page template is missing: $templatePath"
    }
    Copy-Item -LiteralPath $templatePath -Destination $PagePath -Force
    Set-QgcMainPageBackPress $PagePath
    Set-QgcWindowTitleBar $PagePath
}

function Set-QgcWindowCloseHooks([string]$AbilityPath) {
    Assert-NoReparse $AbilityPath
    $abilityText = (Get-Content -LiteralPath $AbilityPath -Raw).Replace("`r`n", "`n")
    $hooks = @(
        @{
            Source = "import qpa from 'libqohos.so';"
            Target = "import qpa from 'libqohos.so';`nimport { QGC_WINDOW_CLOSE_REQUEST } from './QGCWindowTitleBar';"
        },
        @{
            Source = 'export default class QAbility extends UIAbility {'
            Target = "export default class QAbility extends UIAbility {`n  private readonly qgcCloseRequested = (): void => {`n    qpa.handleAbilityOnPrepareToTerminate(this);`n  };"
        },
        @{
            Source = '    qpa.handleAbilityOnCreate(this, want, launchParam);'
            Target = "    qpa.handleAbilityOnCreate(this, want, launchParam);`n    this.context.eventHub.on(QGC_WINDOW_CLOSE_REQUEST, this.qgcCloseRequested);"
        },
        @{
            Source = "    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility::onDestroy()');"
            Target = "    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility::onDestroy()');`n    this.context.eventHub.off(QGC_WINDOW_CLOSE_REQUEST, this.qgcCloseRequested);"
        }
    )
    $staged = $abilityText.Contains('qgcCloseRequested') -or $abilityText.Contains('QGC_WINDOW_CLOSE_REQUEST')
    foreach ($hook in $hooks) {
        $anchor = if ($staged) { $hook.Target } else { $hook.Source }
        if ([regex]::Matches($abilityText, [regex]::Escape($anchor)).Count -ne 1) {
            throw 'The Qt ability must contain one complete close-request event integration anchor.'
        }
    }
    if (-not $staged) {
        foreach ($hook in $hooks) { $abilityText = $abilityText.Replace($hook.Source, $hook.Target) }
        Set-Content -LiteralPath $AbilityPath -Value $abilityText -Encoding utf8 -NoNewline
    }
}

function Set-QgcStartWindow([string]$ModulePath) {
    Assert-NoReparse $ModulePath
    $moduleText = Get-Content -LiteralPath $ModulePath -Raw
    $icons = [regex]::Matches($moduleText, '"icon"\s*:\s*"(\$media:[A-Za-z0-9_]+)"')
    $startIcons = [regex]::Matches($moduleText, '"startWindowIcon"\s*:\s*"[^"]+"')
    $backgrounds = [regex]::Matches($moduleText, '"startWindowBackground"\s*:\s*"(\$color:[A-Za-z0-9_]+)"')
    $profiles = [regex]::Matches($moduleText, '"startWindow"\s*:\s*"[^"]+"')
    if ($icons.Count -ne 1 -or $startIcons.Count -ne 1 -or $backgrounds.Count -ne 1 -or $profiles.Count -gt 1) {
        throw 'The staged module must contain one official icon and one valid startup background.'
    }
    $profile = Get-Content -LiteralPath (Join-Path $projectRoot 'deploy/harmonyos/resources/qgc_start_window.json') -Raw | ConvertFrom-Json
    if ($profile.startWindowType -cne 'REQUIRED_HIDE') { throw 'The startup profile must hide the early system window.' }
    $profile.startWindowAppIcon = $icons[0].Groups[1].Value
    $profile.startWindowBackgroundColor = $backgrounds[0].Groups[1].Value
    $iconEntry = '"startWindowIcon": "' + $profile.startWindowAppIcon + '"'
    $profileEntry = '"startWindow": "$profile:qgc_start_window"'
    if ($profiles.Count -eq 1) {
        $moduleText = $moduleText.Replace($profiles[0].Value, $profileEntry)
    } else {
        $iconEntry = $profileEntry + ",`n        " + $iconEntry
    }
    $moduleText = $moduleText.Replace($startIcons[0].Value, $iconEntry)
    $profilePath = Join-Path (Split-Path $ModulePath -Parent) 'resources/base/profile/qgc_start_window.json'
    Assert-NoReparse $profilePath
    New-Item -ItemType Directory -Path (Split-Path $profilePath -Parent) -Force | Out-Null
    Set-Content -LiteralPath $profilePath -Value ($profile | ConvertTo-Json) -Encoding utf8 -NoNewline
    Set-Content -LiteralPath $ModulePath -Value $moduleText -Encoding utf8 -NoNewline
}

function Set-QgcWindowPolicyHooks([string]$AbilityPath) {
    Assert-NoReparse $AbilityPath
    $abilityText = (Get-Content -LiteralPath $AbilityPath -Raw).Replace("`r`n", "`n")
    $hooks = @(
        @{
            Source = "`nimport Window from '@ohos.window'"
            Target = "`nimport Window from '@ohos.window'`nimport { QGCWindowPolicy } from './QGCWindowPolicy';"
        },
        @{
            Source = "`n  private localStorage: LocalStorage | null = null;"
            Target = "`n  private localStorage: LocalStorage | null = null;`n  private readonly qgcWindowPolicy: QGCWindowPolicy = new QGCWindowPolicy();"
        },
        @{
            Source = "`n    qpa.handleAbilityOnWindowStageCreate(this, windowStage);"
            Target = "`n    this.qgcWindowPolicy.prepare(windowStage, (): void => {`n      qpa.handleAbilityOnWindowStageCreate(this, windowStage);`n    }, this.context);"
        },
        @{
            Source = "`n    qpa.handleAbilityOnWindowStageRestore(this, windowStage);"
            Target = "`n    this.qgcWindowPolicy.prepare(windowStage, (): void => {`n      qpa.handleAbilityOnWindowStageRestore(this, windowStage);`n    });"
        },
        @{
            Source = "`n    qpa.handleAbilityOnForeground(this);"
            Target = "`n    this.qgcWindowPolicy.foreground();`n    qpa.handleAbilityOnForeground(this);"
        },
        @{
            Source = "`n    qpa.handleAbilityOnBackground(this);"
            Target = "`n    this.qgcWindowPolicy.background();`n    qpa.handleAbilityOnBackground(this);"
        },
        @{
            Source = "`n    qpa.handleAbilityOnWindowStageDestroy(this);"
            Target = "`n    this.qgcWindowPolicy.destroy();`n    qpa.handleAbilityOnWindowStageDestroy(this);"
        },
        @{
            Source = "`n    return qpa.handleAbilityOnDestroy(this);"
            Target = "`n    this.qgcWindowPolicy.destroy();`n    return qpa.handleAbilityOnDestroy(this);"
        }
    )
    $staged = $abilityText.Contains('QGCWindowPolicy') -or $abilityText.Contains('qgcWindowPolicy')
    $upgraded = $false
    foreach ($ending in @('});', '}, true);')) {
        $previousCreate = $hooks[2].Target.Replace('}, this.context);', $ending)
        if ($staged -and [regex]::Matches($abilityText, [regex]::Escape($previousCreate)).Count -eq 1) {
            $abilityText = $abilityText.Replace($previousCreate, $hooks[2].Target)
            $upgraded = $true
        }
    }
    if ($staged -and -not $abilityText.Contains('qgcWindowPolicy.background')) {
        if ([regex]::Matches($abilityText, [regex]::Escape($hooks[5].Source)).Count -eq 1) {
            $abilityText = $abilityText.Replace($hooks[5].Source, $hooks[5].Target)
            $upgraded = $true
        }
    }
    foreach ($hook in $hooks) {
        $anchor = if ($staged) { $hook.Target } else { $hook.Source }
        if ([regex]::Matches($abilityText, [regex]::Escape($anchor)).Count -ne 1 -or
            ($staged -and [regex]::Matches($abilityText.Replace($hook.Target, ''), [regex]::Escape($hook.Source)).Count -ne 0)) {
            throw 'The staged QAbility must contain exactly one complete Qt lifecycle anchor per window policy hook.'
        }
    }
    if (-not $staged) {
        foreach ($hook in $hooks) { $abilityText = $abilityText.Replace($hook.Source, $hook.Target) }
    }
    if ([regex]::Matches($abilityText, 'qgcWindowPolicy\.background\s*\(').Count -ne 1) {
        throw 'The staged QAbility must contain exactly one complete background policy hook.'
    }
    if (-not $staged -or $upgraded) {
        Set-Content -LiteralPath $AbilityPath -Value $abilityText -Encoding utf8 -NoNewline
    }
}

foreach ($path in @($buildRoot, $settingsPath, $cachePath, $packageRoot,
        (Join-Path $packageRoot 'entry'), (Join-Path $packageRoot 'oh_modules'),
        (Join-Path $packageRoot 'entry/oh_modules'))) {
    Assert-NoReparse $path
}
foreach ($directory in @($packageRoot, (Join-Path $packageRoot 'entry'))) {
    if (Test-Path -LiteralPath $directory -PathType Container) {
        foreach ($file in Get-ChildItem -LiteralPath $directory -File -Force) {
            Assert-NoReparse $file.FullName
        }
    }
}
$packageWriteTrees = @('AppScope', 'entry/src', 'entry/libs', 'entry/build', 'hvigor', '.hvigor')
foreach ($relative in $packageWriteTrees) { Assert-NoReparse (Join-Path $packageRoot $relative) -Tree }
foreach ($relative in @('cache/home', 'cache/temp', 'cache/npm', 'cache/hvigor')) {
    Assert-NoReparse (Join-Path $tmpRoot $relative) -Tree
}
. (Join-Path $PSScriptRoot 'deveco-env.ps1') -DevEcoPath $DevEcoPath

function Assert-HapArchitecture([string]$HapPath) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($HapPath)
    try {
        $manifestEntry = $archive.GetEntry('module.json')
        if (-not $manifestEntry) { throw 'HAP module.json is missing.' }
        $manifestReader = [IO.StreamReader]::new($manifestEntry.Open())
        try { $manifest = $manifestReader.ReadToEnd() | ConvertFrom-Json } finally { $manifestReader.Dispose() }
        if ($manifest.app.versionName -ne '5.0.8' -or $manifest.app.versionCode -ne $VersionCode -or
                $manifest.app.bundleName -ne $BundleName) {
            throw 'HAP application metadata does not match the selected v5.0.8 package.'
        }
        $libraryPrefix = "libs/$targetArchitecture/"
        $appLibraryEntry = $libraryPrefix + [IO.Path]::GetFileName($nativeLibrary)
        if (-not $archive.GetEntry($appLibraryEntry)) {
            throw "Application library missing from HAP: $appLibraryEntry"
        }
        $expectedMachine = if ($targetArchitecture -eq 'x86_64') { 62 } else { 183 }
        foreach ($entry in $archive.Entries | Where-Object { $_.FullName -match '\.so$' }) {
            if (-not $entry.FullName.StartsWith($libraryPrefix, [StringComparison]::Ordinal)) {
                throw "Unexpected native-library ABI directory: $($entry.FullName)"
            }
            $reader = [IO.BinaryReader]::new($entry.Open())
            try { $header = $reader.ReadBytes(20) } finally { $reader.Dispose() }
            if ($header.Length -ne 20 -or $header[0] -ne 0x7f -or
                    $header[1] -ne 0x45 -or $header[2] -ne 0x4c -or $header[3] -ne 0x46 -or
                    $header[4] -ne 2 -or $header[5] -ne 1 -or
                    [BitConverter]::ToUInt16($header, 18) -ne $expectedMachine) {
                throw "Native library is not a $targetArchitecture ELF: $($entry.FullName)"
            }
        }
    } finally {
        $archive.Dispose()
    }
}

if (-not (Test-Path -LiteralPath $settingsPath)) {
    throw "Configure and build v5.0.8 first: missing $settingsPath"
}
$cache = Get-Content -LiteralPath $cachePath -Raw
$nativeMode = [regex]::Match($cache, '(?m)^CMAKE_BUILD_TYPE:[^=]+=([^\r\n]*)').Groups[1].Value
if ($nativeMode -ine $BuildMode) {
    throw "Native CMAKE_BUILD_TYPE=$nativeMode does not match package mode $BuildMode."
}
$settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
if ($settings.'harmonyos-app-bundle-name' -ne $BundleName) {
    throw 'Deployment settings bundle name must match -BundleName.'
}
$nativeLibrary = [IO.Path]::GetFullPath($settings.'application-binary')
if (-not $nativeLibrary.StartsWith($buildRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The native library must belong to the selected v5.0.8 build directory.'
}
Assert-NoReparse $nativeLibrary
if (-not (Test-Path -LiteralPath $nativeLibrary)) {
    throw "Build the native application first: missing $nativeLibrary"
}
if ($settings.'harmonyos-app-version-name' -ne '5.0.8') {
    throw 'Deployment settings must declare QGroundControl version 5.0.8.'
}
$targetArchitecture = if ($Architecture -eq 'arm64') { 'arm64-v8a' } else { 'x86_64' }
if (@($settings.'harmonyos-target-arch').Count -ne 1 -or
        $settings.'harmonyos-target-arch'[0] -ne $targetArchitecture) {
    throw "Deployment architecture must be $targetArchitecture."
}
$deviceTypes = @($settings.'harmonyos-module-device-types' | Sort-Object)
if (($deviceTypes -join ',') -ne '2in1,phone,tablet') {
    throw 'Deployment settings must enable exactly phone, tablet and 2in1.'
}
if ($settings.'harmonyos-compatible-sdk-version' -ne '6.1.0(23)') {
    throw 'The minimum deployment SDK must be HarmonyOS 6.1 / API 23.'
}
if (-not (Test-Path -LiteralPath $harmonyDeployQt)) {
    throw "Qt staging tool not found: $harmonyDeployQt"
}
if (-not (Test-Path -LiteralPath $DevEcoCli)) {
    throw "Official DevEco CLI not found: $DevEcoCli"
}
if ($Action -eq 'Run' -and [string]::IsNullOrWhiteSpace($Device)) {
    throw 'Run requires -Device with an explicit device name or serial.'
}

$stageArguments = @('--input', $settingsPath, '--output', $packageRoot, '--no-build')
if ($BuildMode -eq 'release') { $stageArguments += '--release' }
$projectProfilePath = Join-Path $packageRoot 'build-profile.json5'
$signingSnapshot = Get-QgcSigningSnapshot $projectProfilePath
& $harmonyDeployQt @stageArguments
if ($LASTEXITCODE -ne 0) { throw "Qt staging failed with exit code $LASTEXITCODE." }
foreach ($relative in $packageWriteTrees) { Assert-NoReparse (Join-Path $packageRoot $relative) -Tree }
Restore-QgcSigningSnapshot $projectProfilePath $signingSnapshot

$abilityRoot = Join-Path $packageRoot 'entry/src/main/ets/qability'
$windowPolicyTarget = Join-Path $abilityRoot 'QGCWindowPolicy.ets'
$windowTitleBarTarget = Join-Path $abilityRoot 'QGCWindowTitleBar.ets'
$qAbilityPath = Join-Path $abilityRoot 'QAbility.ets'
Assert-NoReparse $windowPolicyTarget
Assert-NoReparse $windowTitleBarTarget
Assert-NoReparse $qAbilityPath
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'bridge/QGCWindowPolicy.ets') -Destination $windowPolicyTarget -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'bridge/QGCWindowTitleBar.ets') -Destination $windowTitleBarTarget -Force
Set-QgcWindowPolicyHooks $qAbilityPath
Set-QgcWindowCloseHooks $qAbilityPath
$mainPagePath = Join-Path $packageRoot 'entry/src/main/ets/pages/MainWindowNativeNode.ets'
Initialize-QgcMainPage $mainPagePath $settings.'harmonyos-package-source-directory'

$hvigorConfigPath = Join-Path $packageRoot 'hvigor/hvigor-config.json5'
Assert-NoReparse $hvigorConfigPath
$hvigorConfig = Get-Content -LiteralPath $hvigorConfigPath -Raw | ConvertFrom-Json
$hvigorConfig | Add-Member -NotePropertyName execution -NotePropertyValue ([pscustomobject]@{
    daemon = $false
    parallel = $false
}) -Force
$hvigorConfig | Add-Member -NotePropertyName nodeOptions -NotePropertyValue ([pscustomobject]@{
    maxOldSpaceSize = 4096
}) -Force
Set-Content -LiteralPath $hvigorConfigPath -Value ($hvigorConfig | ConvertTo-Json -Depth 8) -Encoding utf8 -NoNewline

$bridges = @()
if ($cache -match '(?m)^QGC_NO_SERIAL_LINK:BOOL=OFF\r?$') {
    $bridges += 'QGCSerialBridge'
}
if ($cache -match '(?m)^QGC_ENABLE_BLUETOOTH:BOOL=ON\r?$') {
    $bridges += 'QGCBluetoothBridge'
    $bridges += 'QGCBleBridge'
}
foreach ($bridge in $bridges) {
    $bridgeSource = Join-Path $PSScriptRoot "bridge/$bridge.ets"
    $abilityRoot = Join-Path $packageRoot 'entry/src/main/ets/qability'
    $bridgeTarget = Join-Path $abilityRoot "$bridge.ets"
    $qtUtilsPath = Join-Path $abilityRoot 'QtUtils.ets'
    Assert-NoReparse $bridgeTarget
    Assert-NoReparse $qtUtilsPath
    $qtUtils = Get-Content -LiteralPath $qtUtilsPath -Raw
    if ($qtUtils -notmatch ("factories\['" + [regex]::Escape($bridge) + "'\]")) {
        if ([regex]::Matches($qtUtils, 'return factories;').Count -ne 1) {
            throw 'The staged QtUtils must contain one module factory return statement.'
        }
        $qtUtils = "import { get$bridge } from './$bridge';`n" +
            $qtUtils.Replace('return factories;', "factories['$bridge'] = get$bridge;`n    return factories;")
        Set-Content -LiteralPath $qtUtilsPath -Value $qtUtils -Encoding utf8 -NoNewline
    }
    Copy-Item -LiteralPath $bridgeSource -Destination $bridgeTarget -Force
}
if ($bridges -contains 'QGCSerialBridge') {
    foreach ($support in @('QGCUsbSerialTypes', 'QGCCp210xDriver', 'QGCCh34xDriver', 'QGCFtdiDriver', 'QGCCdcDescriptors')) {
        $supportTarget = Join-Path $abilityRoot "$support.ets"
        Assert-NoReparse $supportTarget
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "bridge/$support.ets") -Destination $supportTarget -Force
    }
    $usbNoticeRoot = Join-Path $packageRoot 'entry/src/main/resources/rawfile/third_party/usb-serial-for-android'
    Assert-NoReparse $usbNoticeRoot -Tree
    New-Item -ItemType Directory -Force -Path $usbNoticeRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'bridge/USB_SERIAL_LICENSE.txt') `
        -Destination (Join-Path $usbNoticeRoot 'LICENSE.txt') -Force
    $syscapPath = Join-Path $packageRoot 'entry/src/main/syscap.json'
    Assert-NoReparse $syscapPath
    $syscap = if (Test-Path -LiteralPath $syscapPath) {
        Get-Content -LiteralPath $syscapPath -Raw | ConvertFrom-Json
    } else {
        [pscustomobject]@{
            devices = [pscustomobject]@{ general = @('default', 'tablet') }
            development = [pscustomobject]@{ addedSysCaps = @() }
            production = [pscustomobject]@{ addedSysCaps = @(); removedSysCaps = @() }
        }
    }
    $syscap.development.addedSysCaps = @($syscap.development.addedSysCaps +
        'SystemCapability.USB.USBManager.Serial' | Select-Object -Unique)
    Set-Content -LiteralPath $syscapPath -Value ($syscap | ConvertTo-Json -Depth 5) -Encoding utf8 -NoNewline
}

if ($bridges -contains 'QGCBluetoothBridge') {
    foreach ($locale in @('base', 'zh_CN')) {
        $elementRoot = Join-Path $packageRoot "entry/src/main/resources/$locale/element"
        Assert-NoReparse $elementRoot -Tree
        New-Item -ItemType Directory -Path $elementRoot -Force | Out-Null
        $stringsPath = Join-Path $elementRoot 'string.json'
        $strings = if (Test-Path -LiteralPath $stringsPath) {
            Get-Content -LiteralPath $stringsPath -Raw | ConvertFrom-Json
        } else {
            [pscustomobject]@{ string = @() }
        }
        $reason = if ($locale -eq 'zh_CN') { '用于发现并连接蓝牙飞控设备' }
            else { 'Find and connect to flight controllers over Bluetooth' }
        $strings.string = @($strings.string | Where-Object { $_.name -ne 'qgc_bluetooth_reason' }) +
            @([pscustomobject]@{ name = 'qgc_bluetooth_reason'; value = $reason })
        Set-Content -LiteralPath $stringsPath -Value ($strings | ConvertTo-Json -Depth 5) -Encoding utf8 -NoNewline
    }
}

if ($cache -match '(?m)^QGC_FFMPEG_STREAMING:BOOL=ON\r?$') {
    $ffmpegRoot = [regex]::Match($cache, '(?m)^QGC_FFMPEG_ROOT:[^=]+=([^\r\n]+)').Groups[1].Value
    $manifestPath = Join-Path $ffmpegRoot 'build-manifest.json'
    Assert-NoReparse $manifestPath
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.version -ne '9.0.1' -or $manifest.architecture -ne $Architecture) {
        throw 'FFmpeg manifest must match version 9.0.1 and the selected target architecture.'
    }
    $licenseSource = Join-Path $tmpRoot 'ffmpeg/9.0.1/source/ffmpeg-9.0.1/COPYING.LGPLv2.1'
    Assert-NoReparse $licenseSource
    $noticeRoot = Join-Path $packageRoot 'entry/src/main/resources/rawfile/third_party/ffmpeg'
    Assert-NoReparse $noticeRoot -Tree
    New-Item -ItemType Directory -Force -Path $noticeRoot | Out-Null
    Copy-Item -LiteralPath $licenseSource -Destination (Join-Path $noticeRoot 'COPYING.LGPLv2.1') -Force
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $noticeRoot 'build-manifest.json') -Force
}

$appMetadataPath = Join-Path $packageRoot 'AppScope/app.json5'
Assert-NoReparse $appMetadataPath
$appMetadataText = Get-Content -LiteralPath $appMetadataPath -Raw
$versionCodePattern = '"versionCode"\s*:\s*\d+'
if ([regex]::Matches($appMetadataText, $versionCodePattern).Count -ne 1) {
    throw 'The staged AppScope must contain one numeric versionCode field.'
}
$appMetadataText = [regex]::Replace($appMetadataText, $versionCodePattern, ('"versionCode": ' + $VersionCode))
Set-Content -LiteralPath $appMetadataPath -Value $appMetadataText -Encoding utf8 -NoNewline
$appMetadata = (Get-Content -LiteralPath $appMetadataPath -Raw | ConvertFrom-Json).app
if ($appMetadata.versionCode -ne $VersionCode -or $appMetadata.versionName -ne '5.0.8' -or
        $appMetadata.bundleName -ne $BundleName) {
    throw 'The staged AppScope must retain the selected versionCode, version 5.0.8, and bundle name.'
}

$modulePath = Join-Path $packageRoot 'entry/src/main/module.json5'
if (-not (Test-Path -LiteralPath $modulePath)) {
    throw "Staged entry module not found: $modulePath"
}
Set-QgcStartWindow $modulePath
$moduleText = Get-Content -LiteralPath $modulePath -Raw
$typesMatch = [regex]::Match($moduleText, '"deviceTypes"\s*:\s*(\[[^\]]*\])')
if ($moduleText -match '%%INSERT_' -or -not $typesMatch.Success) {
    throw 'The staged module must have resolved phone, tablet and 2in1 device types.'
}
$stagedTypes = @($typesMatch.Groups[1].Value | ConvertFrom-Json | Sort-Object)
if (($stagedTypes -join ',') -ne '2in1,phone,tablet') {
    throw 'The staged module must enable exactly phone, tablet and 2in1.'
}
$entryProfilePath = Join-Path $packageRoot 'entry/build-profile.json5'
Assert-NoReparse $entryProfilePath
$entryProfile = Get-Content -LiteralPath $entryProfilePath -Raw
$abiPattern = '"abiFilters"\s*:\s*\[[^\]]*\]'
if ([regex]::Matches($entryProfile, $abiPattern).Count -ne 1) {
    throw 'The entry profile must contain one abiFilters array.'
}
$entryProfile = [regex]::Replace($entryProfile, $abiPattern, ('"abiFilters": ["' + $targetArchitecture + '"]'))
Set-Content -LiteralPath $entryProfilePath -Value $entryProfile -Encoding utf8 -NoNewline
$stagedAbi = @([regex]::Match((Get-Content -LiteralPath $entryProfilePath -Raw), '"abiFilters"\s*:\s*(\[[^\]]*\])').Groups[1].Value | ConvertFrom-Json)
if (@($stagedAbi).Count -ne 1 -or $stagedAbi[0] -ne $targetArchitecture) {
    throw "The staged entry ABI must be exactly $targetArchitecture."
}
if ($Action -eq 'Stage') {
    Write-Output "Staged v5.0.8 $targetArchitecture project: $packageRoot"
    return
}

Push-Location $packageRoot
try {
    & $DevEcoCli build --modules entry --build-mode $BuildMode
    if ($LASTEXITCODE -ne 0) { throw "DevEco CLI build failed with exit code $LASTEXITCODE." }
    $hapFiles = @(Get-ChildItem -LiteralPath (Join-Path $packageRoot 'entry/build/default/outputs/default') -Filter '*.hap' -File)
    if ($hapFiles.Count -eq 0) { throw 'DevEco CLI produced no entry HAP.' }
    foreach ($hapFile in $hapFiles) {
        Assert-HapArchitecture $hapFile.FullName
        Write-Output "Verified v5.0.8 code $VersionCode and $targetArchitecture native libraries: $($hapFile.FullName)"
    }
    if ($Action -eq 'Run') {
        & $DevEcoCli run --module entry --ability QAbility --device $Device --build-mode $BuildMode --skip-build
        if ($LASTEXITCODE -ne 0) { throw "DevEco CLI run failed with exit code $LASTEXITCODE." }
    }
} finally {
    Pop-Location
}
