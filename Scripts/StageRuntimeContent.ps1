param(
    [Parameter(Mandatory = $true)][string]$RuntimeDirectory,
    [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
    [switch]$IncludeTemplates
)

$ErrorActionPreference = 'Stop'
$RuntimePath = [IO.Path]::GetFullPath($RuntimeDirectory)
$WorkspacePath = [IO.Path]::GetFullPath($WorkspaceRoot)
$Comparison = [StringComparison]::OrdinalIgnoreCase

function Sync-RuntimeContent([string]$RelativePath, [string]$RequiredFile) {
    $SourcePath = [IO.Path]::GetFullPath((Join-Path $WorkspacePath $RelativePath))
    $DestinationPath = [IO.Path]::GetFullPath((Join-Path $RuntimePath $RelativePath))

    if (-not (Test-Path -LiteralPath (Join-Path $SourcePath $RequiredFile) -PathType Leaf)) {
        throw "The source content is missing in $SourcePath."
    }

    $Separator = [IO.Path]::DirectorySeparatorChar
    $RuntimePrefix = $RuntimePath.TrimEnd($Separator) + $Separator

    if ($DestinationPath.Equals($SourcePath, $Comparison) -or $DestinationPath.StartsWith($SourcePath + $Separator, $Comparison) -or $SourcePath.StartsWith($DestinationPath + $Separator, $Comparison) -or -not $DestinationPath.StartsWith($RuntimePrefix, $Comparison)) {
        throw "The runtime content must be separate from the source content: $DestinationPath."
    }

    for ($Directory = $DestinationPath; $Directory.StartsWith($RuntimePrefix, $Comparison); $Directory = [IO.Path]::GetDirectoryName($Directory)) {
        $Item = Get-Item -LiteralPath $Directory -Force -ErrorAction SilentlyContinue

        if ($Item -and ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "The runtime content path cannot be a link: $Directory."
        }
    }

    & robocopy.exe $SourcePath $DestinationPath /MIR /XJ /R:1 /W:1 /NFL /NDL /NJH /NJS /NP

    if ($LASTEXITCODE -ge 8) {
        throw "Content staging failed with robocopy exit code $LASTEXITCODE."
    }
}

Sync-RuntimeContent 'Engine/Content' 'Fonts/Default.json'
Sync-RuntimeContent 'Engine/Shaders' 'Lit.hlsl'

if ($IncludeTemplates) {
    Sync-RuntimeContent 'Templates' 'Project/Template.mproject'
}

exit 0
