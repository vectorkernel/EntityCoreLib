# VisualStudioClean.ps1
# SAFE VERSION
# Cleans ONLY classic build/cache folders under project roots.
# Does NOT touch:
#   - any .vs folder
#   - anything under any .vs folder
#   - any other metadata/config files

$rootPath = "C:\Users\vecto\source\repos"

# Only classic build folders
$buildFolders = @(
    "bin", "obj",
    "x64", "x86",
    "Debug", "Release",
    "ipch", "out", "GeneratedFiles",
    "CMakeFiles"
)

function Is-VSFolder {
    param([string]$PathValue)

    if ([string]::IsNullOrWhiteSpace($PathValue)) { return $false }

    $name = Split-Path $PathValue -Leaf
    return ($name -ieq ".vs")
}

function Is-UnderVSFolder {
    param([string]$PathValue)

    $norm = [System.IO.Path]::GetFullPath($PathValue)

    while ($norm -and (Test-Path $norm)) {
        if (Is-VSFolder $norm) { return $true }
        $parent = Split-Path $norm -Parent
        if ($parent -eq $norm) { break }
        $norm = $parent
    }

    return $false
}

function Remove-DirectorySafely {
    param(
        [string]$TargetPath,
        [string]$Color = "Yellow",
        [string]$Label = "Removing"
    )

    if (-not (Test-Path -LiteralPath $TargetPath)) { return }

    # Absolute safety: never touch .vs or anything under it
    if (Is-VSFolder $TargetPath -or Is-UnderVSFolder $TargetPath) {
        Write-Host "Skipping .vs-protected path: $TargetPath" -ForegroundColor Magenta
        return
    }

    Write-Host "$Label $TargetPath" -ForegroundColor $Color
    Remove-Item -Recurse -Force -LiteralPath $TargetPath -ErrorAction SilentlyContinue
}

Write-Host "Root path: $rootPath" -ForegroundColor Cyan
Write-Host "Cleaning ONLY classic build folders. .vs and metadata are untouched." -ForegroundColor Cyan

# Find project roots (contain .csproj or .vcxproj)
$projectRoots = Get-ChildItem -Path $rootPath -Recurse -Directory -Force -ErrorAction SilentlyContinue |
    Where-Object {
        (Get-ChildItem -Path $_.FullName -Filter *.csproj  -File -ErrorAction SilentlyContinue | Select-Object -First 1) -or
        (Get-ChildItem -Path $_.FullName -Filter *.vcxproj -File -ErrorAction SilentlyContinue | Select-Object -First 1)
    }

foreach ($proj in $projectRoots) {
    Write-Host "Cleaning project root: $($proj.FullName)" -ForegroundColor Cyan

    foreach ($pattern in $buildFolders) {
        $matches = Get-ChildItem -Path $proj.FullName -Directory -Recurse -Force -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -ieq $pattern }

        foreach ($dir in $matches) {
            Remove-DirectorySafely -TargetPath $dir.FullName -Color Yellow -Label "Removing build folder:"
        }
    }
}

Write-Host "Build folder cleanup done. No .vs or startup data touched." -ForegroundColor Green

