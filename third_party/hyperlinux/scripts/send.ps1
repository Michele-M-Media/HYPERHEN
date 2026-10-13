<#
.SYNOPSIS
  Send a payload to the PS5 payload loader (elfldr, default port 9021).

.DESCRIPTION
  Reads the payload ELF as raw bytes and writes them to the console's payload
  loader over TCP, then closes the connection so the loader knows the payload
  ended.

.PARAMETER Payload
  Path to the payload ELF. Defaults to ..\bin\pLinux5-NG.elf relative to this
  script.

.PARAMETER PS5IP
  Console IP. Defaults to $env:PS5_IP, or 192.168.1.159.

.PARAMETER Port
  Loader port. Defaults to $env:PS5_PORT, or 9021.

.EXAMPLE
  ./scripts/send.ps1
.EXAMPLE
  ./scripts/send.ps1 -PS5IP 192.168.1.50 -Payload ..\bin\pLinux5-NG.elf
#>
param(
  [string]$Payload,
  [string]$PS5IP = $(if ($env:PS5_IP) { $env:PS5_IP } else { '192.168.1.159' }),
  [int]$Port = $(if ($env:PS5_PORT) { [int]$env:PS5_PORT } else { 9021 })
)

$ErrorActionPreference = 'Stop'

if (-not $Payload) {
  $Payload = Join-Path $PSScriptRoot '..\bin\pLinux5-NG.elf'
}
$Payload = (Resolve-Path $Payload).Path

$bytes = [System.IO.File]::ReadAllBytes($Payload)
Write-Host ("sending {0} ({1} bytes) -> {2}:{3}" -f (Split-Path $Payload -Leaf), $bytes.Length, $PS5IP, $Port)

$client = New-Object System.Net.Sockets.TcpClient
$client.Connect($PS5IP, $Port)
try {
  $stream = $client.GetStream()
  $stream.Write($bytes, 0, $bytes.Length)
  $stream.Flush()
} finally {
  $client.Close()
}

Write-Host 'done'
