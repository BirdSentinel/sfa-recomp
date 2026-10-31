$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pin = Get-Content -Raw -LiteralPath (Join-Path $root 'patches/rexglue-base.json') | ConvertFrom-Json
$sdk = Join-Path $root 'tools/rexglue-sdk-terrain'
$patch = Join-Path $root 'patches/rexglue-rtv.patch'
if (!(Test-Path -LiteralPath $sdk)) {
  git clone --no-checkout $pin.repository $sdk
  if ($LASTEXITCODE) { throw 'SDK clone failed' }
  git -C $sdk checkout --detach $pin.commit
  if ($LASTEXITCODE) { throw 'SDK checkout failed' }
}
$head = git -C $sdk rev-parse HEAD
if ($LASTEXITCODE -or $head -ne $pin.commit) { throw 'SDK checkout does not match the pinned commit' }
git -C $sdk diff --quiet HEAD
if ($LASTEXITCODE -eq 1) {
  git -C $sdk apply --reverse --check $patch
  if ($LASTEXITCODE) { throw 'SDK has changes that do not match the patch; review them first' }
  Write-Output 'SDK patch already applied.'
} elseif ($LASTEXITCODE -eq 0) {
  git -C $sdk apply --check $patch
  if ($LASTEXITCODE) { throw 'SDK patch cannot apply cleanly; preserve and review existing changes' }
  git -C $sdk apply $patch
  if ($LASTEXITCODE) { throw 'SDK patch failed' }
} else {
  throw 'Cannot inspect SDK working tree'
}
git -C $sdk submodule update --init --recursive
if ($LASTEXITCODE) { throw 'SDK submodule setup failed' }
Write-Output 'Pinned RTV-only SDK source is ready.'
