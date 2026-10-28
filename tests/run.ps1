param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$testBuild = Join-Path $projectRoot 'build/host-tests'
New-Item -ItemType Directory -Force $testBuild | Out-Null
& $Compiler -std=c11 -Wall -Wextra -Werror -g -I (Join-Path $projectRoot 'src') (Join-Path $PSScriptRoot 'test_protocols.c') (Join-Path $projectRoot 'src/gamepad.c') (Join-Path $projectRoot 'src/dhcp_server.c') -o (Join-Path $testBuild 'protocols.exe')
if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed' }
& (Join-Path $testBuild 'protocols.exe')
if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }
