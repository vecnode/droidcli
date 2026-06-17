param(
    [Parameter(Mandatory = $true)][string]$InputPng,
    [Parameter(Mandatory = $true)][string]$OutputIco
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$bitmap = [System.Drawing.Bitmap]::FromFile($InputPng)
try {
    $icon = [System.Drawing.Icon]::FromHandle($bitmap.GetHicon())
    try {
        $stream = [System.IO.File]::Open($OutputIco, [System.IO.FileMode]::Create)
        try {
            $icon.Save($stream)
        } finally {
            $stream.Close()
        }
    } finally {
        $icon.Dispose()
    }
} finally {
    $bitmap.Dispose()
}
