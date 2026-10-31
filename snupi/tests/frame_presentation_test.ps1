param(
  [Parameter(Mandatory=$true)][string]$Executable,
  [Parameter(Mandatory=$true)][string]$GameRoot,
  [int[]]$Rates = @(30,60,90,120,144,165,180,240)
)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
# Separate preferences/cache: never change the player's frame cap or saves.
$testRoot = Join-Path $env:TEMP ("snupi-fps-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $testRoot | Out-Null
foreach ($rate in $Rates) {
  Set-Content -LiteralPath (Join-Path $testRoot 'framerate.txt') -Value $rate
  $log = Join-Path $testRoot "host-$rate.log"
  $process = Start-Process -FilePath $Executable -WorkingDirectory $GameRoot -WindowStyle Hidden -PassThru -ArgumentList @(
    '-skipintro', "--game_data_root=`"$GameRoot`"", "--user_data_root=`"$testRoot`"",
    '--no-fullscreen', '--render_target_path_d3d12=rtv', '--audio_mute=true',
    '--frame_limit_audit=true', '--log_level=info', "--log_file=`"$log`""
  )
  try {
    Start-Sleep -Seconds 15
    if ($process.HasExited) { throw "Game exited during $rate FPS test: $log" }
  } finally {
    if (!$process.HasExited) { Stop-Process -Id $process.Id }
  }
  # Guest-only measurements cannot detect the monitor-rate repaint regression.
  $samples = @(Get-Content -LiteralPath $log | ForEach-Object {
    if ($_ -match 'Frame pacing \(host\): limit (\d+), measured ([\d.]+) fps') {
      if ([int]$Matches[1] -ne $rate) { throw "Wrong cap loaded: $_" }
      [double]::Parse($Matches[2], [Globalization.CultureInfo]::InvariantCulture)
    }
  })
  if ($samples.Count -lt 3) { throw "Insufficient host presentation samples: $log" }
  # Slow frames may fall below the cap; duplicate UI presentations must never
  # push the measured rate above it. Allow rounding of the audit's two decimals.
  if (@($samples | Where-Object { $_ -gt ($rate + 0.05) }).Count) {
    throw "Host presentation exceeded $rate FPS: $($samples -join ', '); $log"
  }
  Write-Output "$rate FPS cap passed: $($samples -join ', ')"
}
Write-Output "Host presentation logs: $testRoot"
