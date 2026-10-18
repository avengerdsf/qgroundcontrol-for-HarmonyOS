param([string]$ToolchainRoot)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $ToolchainRoot) { $ToolchainRoot = $projectRoot }
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $projectRoot 'deploy/harmonyos/package.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { throw ($parseErrors | Out-String) }
$hookFunction = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Set-QgcWindowTitleBar'
}, $false)
if (-not $hookFunction) { throw 'Packaging must provide reproducible native title bar integration.' }
Invoke-Expression $hookFunction.Extent.Text
foreach ($functionName in @('Set-QgcMainPageBackPress', 'Initialize-QgcMainPage')) {
    $functionAst = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName
    }, $false)
    if (-not $functionAst) { throw "Packaging function is missing: $functionName" }
    Invoke-Expression $functionAst.Extent.Text
}
function Assert-NoReparse([string]$Path) {
    if (-not $Path.StartsWith($fixtureRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture write escaped project tmp.' }
}
$fixtureRoot = Join-Path $projectRoot "tmp/verification/window-titlebar-fixtures-$PID"
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
try {
    $source = @'
interface WindowNativeNodeCreateInfo {
  xComponentId: string
  onAppear: () => void
  onAttach: () => void
  onDisAppear: () => void
}

@Entry
@Component
struct WindowNativeNode {
  @State private createInfo: WindowNativeNodeCreateInfo | undefined = undefined

  onBackPress(): boolean {
    console.info('QGC main page: system Back consumed');
    return true;
  }

  aboutToAppear(): void {
    const localStorage  = this.getUIContext().getSharedLocalStorage();
    if (localStorage ) {
      this.createInfo = localStorage.get<WindowNativeNodeCreateInfo>('createInfo');
    }
  }

  build() {
    Stack() {
      if (this.createInfo !== undefined) {
        XComponent({
          type: XComponentType.NODE,
          id: this.createInfo.xComponentId,
          libraryname: 'qohos'
        })
        .onAttach(this.createInfo.onAttach)
        .onAppear(this.createInfo.onAppear)
        .onDisAppear(this.createInfo.onDisAppear)
        .width('100%')
        .height('100%')
      }
    }
    .width('100%')
    .height('100%')
  }
}
'@
    $source = $source.Replace("`r`n", "`n")
    $page = Join-Path $fixtureRoot 'MainWindowNativeNode.ets'
    Set-Content -LiteralPath $page -Value $source -Encoding utf8 -NoNewline
    Set-QgcWindowTitleBar $page
    $once = Get-Content -LiteralPath $page -Raw
    Set-QgcWindowTitleBar $page
    if ((Get-Content -LiteralPath $page -Raw) -cne $once) { throw 'Native title staging must be idempotent.' }
    foreach ($preserved in @('.onAttach(this.createInfo.onAttach)', '.onAppear(this.createInfo.onAppear)',
            '.onDisAppear(this.createInfo.onDisAppear)', "id: this.createInfo.xComponentId", "libraryname: 'qohos'",
            "this.createInfo = localStorage.get<WindowNativeNodeCreateInfo>('createInfo');", "onBackPress(): boolean")) {
        if ([regex]::Matches($once, [regex]::Escape($preserved)).Count -ne 1) { throw "Qt page callback changed: $preserved" }
    }
    if ([regex]::Matches($once, 'QGCWindowTitleBar\(\{').Count -ne 1 -or
            [regex]::Matches($once, '\.layoutWeight\(1\)').Count -ne 1 -or
            [regex]::Matches($once, 'window.findWindow\(windowName\)').Count -ne 1 -or
            [regex]::Matches($once, 'context.windowStage.getMainWindowSync\(\).getWindowProperties\(\).id').Count -ne 1 -or
            [regex]::Matches($once, 'uiContext.getWindowId\(\) !== mainWindowId').Count -ne 1 -or
            [regex]::Matches($once, '\.onAreaChange\(\(\): void => \{ this.refreshTitleBarLayout\(\); \}\)').Count -ne 1 -or
            $once.Contains('getLastWindow')) { throw 'Native header, weighted Qt surface or exact UI window lookup is missing.' }
    Write-Output 'PASS native title bar stages once and preserves Qt native surface, lifecycle and Back handling'
    foreach ($bad in @($source.Replace('    Stack() {', '    Row() {'),
            $source.Replace('        .width(''100%'')', ''),
            $once.Replace('this.titleBarController.destroy();', ''))) {
        Set-Content -LiteralPath $page -Value $bad -Encoding utf8 -NoNewline
        $failed = $false
        try { Set-QgcWindowTitleBar $page } catch { $failed = $true }
        if (-not $failed) { throw 'Unknown or partial native node page must fail staging.' }
        if ((Get-Content -LiteralPath $page -Raw) -cne $bad) { throw 'Invalid template was partially modified.' }
    }
    Write-Output 'PASS missing or partial native-node staging anchors fail before modification'
    $templateRoot = Join-Path $ToolchainRoot 'tmp/toolchains/Qt/6.12.0/harmonyos_arm64_v8a/src/harmonyos/templates'
    $oldInjectedPage = $once.Replace('context.windowStage.getMainWindowSync().getWindowProperties().id', 'legacy525MainWindowId')
    Set-Content -LiteralPath $page -Value $oldInjectedPage -Encoding utf8 -NoNewline
    Initialize-QgcMainPage $page $templateRoot
    $rebuilt = (Get-Content -LiteralPath $page -Raw).TrimEnd("`n")
    if ($rebuilt -cne $once.TrimEnd("`n")) { throw 'Re-staging an old injected page did not reproduce current Qt template hooks.' }
    Initialize-QgcMainPage $page $templateRoot
    if ((Get-Content -LiteralPath $page -Raw).TrimEnd("`n") -cne $rebuilt) { throw 'Repeated clean-template staging changed generated content.' }
    Write-Output 'PASS old injected page is rebuilt deterministically from the real clean Qt template'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    $allowed = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp/verification')) + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid fixture cleanup path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
