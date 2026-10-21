function Add-QgcGStreamerNotices([string]$SdkRoot, [string]$Destination, [string]$Architecture) {
    Assert-NoReparse $SdkRoot
    $manifestPath = Join-Path $SdkRoot 'build-manifest.json'
    Assert-NoReparse $manifestPath
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.version -ne '1.28.4' -or $manifest.architecture -ne $Architecture -or
            $manifest.defaultLibrary -ne 'static') {
        throw 'GStreamer manifest must match version 1.28.4, the selected architecture, and static linkage.'
    }
    $licenseRoot = Join-Path $SdkRoot 'share/licenses'
    Assert-NoReparse $licenseRoot -Tree
    if (-not (Test-Path -LiteralPath $licenseRoot -PathType Container)) {
        throw 'GStreamer SDK license directory is missing.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $licenseRoot 'libffi/copyright') -PathType Leaf)) {
        throw 'The static libffi copyright notice is missing from the GStreamer SDK.'
    }
    Assert-NoReparse $Destination -Tree
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $licenseRoot -File -Recurse) {
        # [IO.Path]::GetRelativePath() is .NET Core only; this shell is Windows PowerShell 5.1.
        $basePrefix = [IO.Path]::GetFullPath($licenseRoot).TrimEnd('\') + '\'
        $fileFull = [IO.Path]::GetFullPath($file.FullName)
        if (-not $fileFull.StartsWith($basePrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "License file is outside the SDK license directory: $($file.FullName)"
        }
        $relative = $fileFull.Substring($basePrefix.Length)
        $target = Join-Path (Join-Path $Destination 'licenses') $relative
        Assert-NoReparse $target
        New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target -Force
    }
    $sources = @()
    $sourceLock = Join-Path $SdkRoot 'share/qgc-gstreamer/sources-lock.json'
    $codecLock = Join-Path $SdkRoot 'share/qgc-gstreamer/codecs-lock.json'
    Assert-NoReparse $sourceLock
    Assert-NoReparse $codecLock
    foreach ($source in (Get-Content -LiteralPath $sourceLock -Raw | ConvertFrom-Json)) {
        $sources += [ordered]@{ name = $source.name; version = $source.version; url = $source.url; sha256 = $source.sha256 }
    }
    $codecSources = (Get-Content -LiteralPath $codecLock -Raw | ConvertFrom-Json).sources
    foreach ($property in $codecSources.PSObject.Properties) {
        $source = $property.Value
        $sources += [ordered]@{
            name = $property.Name; version = $source.version; url = $source.url
            sha256 = $source.sha256; license = $source.license
        }
    }
    [ordered]@{
        version = $manifest.version
        architecture = $manifest.architecture
        plugins = $manifest.plugins
        gioModules = $manifest.gioModules
        sources = $sources
        recipe = 'https://github.com/avengerdsf/qgroundcontrol-for-HarmonyOS/tree/Stable_V5.1/deploy/harmonyos/gstreamer'
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Destination 'sources.json') -Encoding utf8
}
