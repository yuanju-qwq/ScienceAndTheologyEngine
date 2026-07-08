# Pre-download third-party source archives via PowerShell.
# This avoids cmake's curl/schannel issues with GitHub redirects.
# Run this before cmake configure:
#   powershell -ExecutionPolicy Bypass -File snt_engine/cmake/download_third_party.ps1

param(
    [string]$OutDir = "$PSScriptRoot/../third_party/_downloads"
)

$ErrorActionPreference = "Stop"

# Create output directory if missing.
if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

# Downloads list: name, url, expected file name.
# Use codeload.github.com directly to avoid GitHub archive redirect issues
# with some proxies (clash-verge etc.).
$downloads = @(
    @{
        Name = "VulkanHeaders"
        Url  = "https://codeload.github.com/KhronosGroup/Vulkan-Headers/zip/refs/tags/v1.3.295"
        File = "VulkanHeaders-v1.3.295.zip"
    },
    @{
        Name = "VulkanMemoryAllocator"
        Url  = "https://codeload.github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/zip/refs/tags/v3.1.0"
        File = "VulkanMemoryAllocator-v3.1.0.zip"
    },
    @{
        Name = "EnTT"
        Url  = "https://codeload.github.com/skypjack/entt/zip/refs/tags/v3.13.2"
        File = "EnTT-v3.13.2.zip"
    },
    @{
        Name = "stb"
        Url  = "https://codeload.github.com/nothings/stb/zip/refs/heads/master"
        File = "stb-master.zip"
    },
    @{
        Name = "nlohmann_json"
        Url  = "https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz"
        File = "nlohmann_json-v3.11.3.tar.xz"
    },
    @{
        Name = "SDL3"
        Url  = "https://codeload.github.com/libsdl-org/SDL/zip/refs/tags/release-3.4.10"
        File = "SDL3-release-3.4.10.zip"
    },
    @{
        Name = "GLM"
        Url  = "https://codeload.github.com/g-truc/glm/zip/refs/tags/1.0.1"
        File = "glm-1.0.1.zip"
    },
    @{
        Name = "tinyobjloader"
        Url  = "https://codeload.github.com/tinyobjloader/tinyobjloader/zip/refs/heads/release"
        File = "tinyobjloader-release.zip"
    },
    @{
        Name = "googletest"
        Url  = "https://codeload.github.com/google/googletest/zip/refs/tags/v1.14.0"
        File = "googletest-v1.14.0.zip"
    }
)

foreach ($d in $downloads) {
    $outPath = Join-Path $OutDir $d.File
    if (Test-Path $outPath) {
        $size = (Get-Item $outPath).Length
        if ($size -gt 1000) {
            Write-Host "[skip] $($d.Name) already exists ($size bytes)"
            continue
        }
    }
    Write-Host "[download] $($d.Name) from $($d.Url) ..."
    try {
        Invoke-WebRequest -Uri $d.Url -OutFile $outPath -UseBasicParsing -MaximumRedirection 5
        $size = (Get-Item $outPath).Length
        if ($size -lt 1000) {
            Write-Error "Downloaded file too small ($size bytes), likely an error page."
            exit 1
        }
        Write-Host "[ok] $($d.Name) downloaded: $size bytes"
    } catch {
        Write-Error "Failed to download $($d.Name): $_"
        exit 1
    }
}

Write-Host "`nAll downloads complete in: $OutDir"
Get-ChildItem $OutDir | Format-Table Name, Length
