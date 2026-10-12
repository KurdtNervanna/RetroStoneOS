# Read the reserved first-boot trace from an RS1/RS2 card without mounting
# or modifying it. Run disk mode from an Administrator PowerShell window.
# Example: Get-Disk
#   .\Get-RetroStoneFirstBootLog.ps1 -PhysicalDrive 3 -OutputPath .\rs1-firstboot.txt
# ImagePath is for an uncompressed backup image or an offline test fixture.
[CmdletBinding(DefaultParameterSetName = 'Disk')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Disk')]
    [ValidateRange(0, 255)]
    [int] $PhysicalDrive,
    [Parameter(Mandatory, ParameterSetName = 'Image')]
    [string] $ImagePath,
    [string] $OutputPath = '.\retrostone-firstboot.txt'
)

$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $OutputPath) {
    throw "Output already exists: $OutputPath. Choose another filename."
}
if ($PSCmdlet.ParameterSetName -eq 'Disk') {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Open PowerShell as Administrator to read the SD card.'
    }
    $disk = Get-Disk -Number $PhysicalDrive
    if ($disk.IsBoot -or $disk.IsSystem) {
        throw 'Refusing to read a Windows boot/system disk. Select the SD card number from Get-Disk.'
    }
    if ([string] $disk.BusType -notin @('USB', 'SD', 'MMC')) {
        throw "Expected a USB/SD/MMC card reader; selected disk uses $($disk.BusType)."
    }
    Write-Host ("Reading disk {0}: {1}, {2:N1} GB, {3}" -f $disk.Number, $disk.FriendlyName,
        ($disk.Size / 1GB), $disk.BusType)
    $source = "\\.\PhysicalDrive$PhysicalDrive"
} else {
    $source = (Resolve-Path -LiteralPath $ImagePath).ProviderPath
}

# Offset agrees with board.ini firstboot_trace_kib=3072 and genimage.cfg.
# FileAccess.Read is intentional: no format, repair, write or trace clearing.
$stream = [IO.FileStream]::new($source, [IO.FileMode]::Open, [IO.FileAccess]::Read,
    [IO.FileShare]::ReadWrite)
try {
    $null = $stream.Seek(3MB, [IO.SeekOrigin]::Begin)
    $bytes = [byte[]]::new(64KB)
    $total = 0
    while ($total -lt $bytes.Length) {
        $count = $stream.Read($bytes, $total, $bytes.Length - $total)
        if ($count -eq 0) { throw 'Card/image ended before the reserved trace could be read.' }
        $total += $count
    }
} finally {
    $stream.Dispose()
}
if ([Text.Encoding]::ASCII.GetString($bytes, 0, 8) -ne 'RSOSFB01') {
    throw 'No first-boot trace found. The boot may have failed before data setup, or the trace was already exported to rsos/logs/firstboot.txt. Save any existing logs; UART capture is needed if none exist.'
}
$trace = [Text.Encoding]::UTF8.GetString($bytes).Replace([string][char]0, '')
$destination = [IO.Path]::GetFullPath($OutputPath)
[IO.File]::WriteAllText($destination, $trace, [Text.UTF8Encoding]::new($false))
Write-Host "Saved $destination. The card was only read."
