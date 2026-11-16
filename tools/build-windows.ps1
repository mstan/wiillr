# Thin host wrapper; tools/build.py owns configure/build/package behavior.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$pythonName = if ($env:WIILLR_PYTHON) { $env:WIILLR_PYTHON } else { 'python' }
$python = Get-Command $pythonName -ErrorAction SilentlyContinue
if ($null -eq $python) { throw 'Python 3 is required to run tools/build.py.' }
& $python.Source (Join-Path $repo 'tools/build.py') @args
exit $LASTEXITCODE
