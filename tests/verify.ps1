param([string]$Python = "python")

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$containerName = "connect-four-tests-" + [guid]::NewGuid().ToString("N")

function Invoke-Checked {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable failed with exit code $LASTEXITCODE"
    }
}

Push-Location $projectRoot
try {
    Invoke-Checked "docker" @("compose", "build", "server", "client")
    $configText = & docker compose config --format json
    if ($LASTEXITCODE -ne 0) { throw "Cannot resolve Compose project configuration" }
    $config = $configText | ConvertFrom-Json
    $serverImage = if ($config.services.server.image) { $config.services.server.image } else { "$($config.name)-server" }
    $clientImage = if ($config.services.client.image) { $config.services.client.image } else { "$($config.name)-client" }
    # Mount sources read-only; the sanitizer executable lives inside the
    # disposable container and never appears in the project checkout.
    Invoke-Checked "docker" @("run", "--rm", "--mount", "type=bind,source=$projectRoot,target=/workspace,readonly", "--entrypoint", "sh", $serverImage.Trim(), "-c", "gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -g -fsanitize=address,undefined -I/workspace/server/include /workspace/tests/game_test.c /workspace/server/src/game.c -o /tmp/game_test && /tmp/game_test")
    Invoke-Checked "docker" @("run", "--rm", "--mount", "type=bind,source=$projectRoot,target=/workspace,readonly", "--entrypoint", "sh", $serverImage.Trim(), "-c", "gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -g -fsanitize=address,undefined -pthread -I/workspace/server/include /workspace/tests/net_test.c /workspace/server/src/net.c -Wl,--wrap=recv -o /tmp/net_test && /tmp/net_test")
    Invoke-Checked "docker" @("run", "--rm", "--entrypoint", "python3", $clientImage.Trim(), "-c", "import ast; from pathlib import Path; ast.parse(Path('/app/client.py').read_text()); print('PASS client Python syntax')")
    Invoke-Checked "docker" @("run", "-d", "--name", $containerName, "-p", "127.0.0.1::5555", $serverImage.Trim())
    $mapping = & docker port $containerName "5555/tcp"
    if ($LASTEXITCODE -ne 0 -or $mapping -notmatch '^127\.0\.0\.1:(\d+)$') { throw "Cannot resolve isolated test port: $mapping" }
    $port = $Matches[1]
    Invoke-Checked $Python @((Join-Path $PSScriptRoot "protocol_test.py"), "--host", "127.0.0.1", "--port", $port)
    $status = & docker inspect -f '{{.State.Running}}' $containerName
    if ($LASTEXITCODE -ne 0 -or $status -ne "true") { throw "Server stopped during the tests" }
    Write-Host "PASS complete verification; isolated container remained running"
}
finally {
    & docker logs $containerName 2>$null
    & docker rm -f $containerName 2>$null | Out-Null
    Pop-Location
}
