[CmdletBinding()]
param(
    [string]$UEProjectPath,
    [switch]$SkipUvInstall,
    [switch]$SkipConnectionTest
)

# One-time, user-level Unreal MCP setup for Windows + Codex Desktop.
# This script does not depend on the current UE project directory.

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$UnrealMcpSource = "git+https://github.com/groscy/unreal-mcp"

function Write-Step([string]$Message) {
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Write-Utf8([string]$Path, [string]$Content) {
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $encoding)
}

function Resolve-Uv {
    $candidates = @()
    $command = Get-Command uv -ErrorAction SilentlyContinue
    if ($command) { $candidates += $command.Source }
    $candidates += (Join-Path $env:USERPROFILE ".local\bin\uv.exe")

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $candidate)) { continue }
        try {
            & $candidate --version *> $null
            if ($LASTEXITCODE -eq 0) { return $candidate }
        } catch {
            # A stale App Installer alias can exist on PATH; try the next candidate.
        }
    }

    if ($SkipUvInstall) {
        throw "uv is not available. Install uv first or run this script without -SkipUvInstall."
    }

    Write-Step "Installing uv for the current Windows user"
    $installer = Join-Path $env:TEMP ("install_uv_" + [guid]::NewGuid().ToString("N") + ".ps1")
    try {
        Invoke-WebRequest -Uri "https://astral.sh/uv/install.ps1" -OutFile $installer
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $installer
        if ($LASTEXITCODE -ne 0) { throw "uv installer exited with code $LASTEXITCODE" }
    } finally {
        Remove-Item -LiteralPath $installer -Force -ErrorAction SilentlyContinue
    }

    $installed = Join-Path $env:USERPROFILE ".local\bin\uv.exe"
    if (-not (Test-Path -LiteralPath $installed)) {
        throw "uv installation finished but $installed was not found."
    }
    return $installed
}

function Install-UnrealMcp([string]$UvPath, [string]$Source) {
    Write-Step "Installing the latest Unreal MCP from $Source in uv's user tool environment"
    & $UvPath tool install --force --reinstall $Source
    if ($LASTEXITCODE -ne 0) {
        throw "uv could not install unreal-mcp. Check network access and try again."
    }

    $toolRoot = (& $UvPath tool dir | Select-Object -Last 1).ToString().Trim()
    if ([string]::IsNullOrWhiteSpace($toolRoot)) { throw "uv tool dir returned no path." }

    $python = Join-Path $toolRoot "unreal-mcp\Scripts\python.exe"
    if (-not (Test-Path -LiteralPath $python)) {
        throw "The Unreal MCP Python runtime was not found at $python."
    }
    return $python
}

function Set-CodexUnrealMcpConfig([string]$PythonPath, [string]$LauncherPath) {
    $codexDir = Join-Path $env:USERPROFILE ".codex"
    $configPath = Join-Path $codexDir "config.toml"
    New-Item -ItemType Directory -Force -Path $codexDir | Out-Null

    $text = if (Test-Path -LiteralPath $configPath) {
        [System.IO.File]::ReadAllText($configPath)
    } else { "" }

    if (Test-Path -LiteralPath $configPath) {
        Copy-Item -LiteralPath $configPath -Destination ($configPath + ".before-unreal-mcp") -Force
    }

    # Remove an earlier Unreal MCP table, including its env subtable, so reruns
    # remain idempotent and do not create duplicate server definitions.
    $sectionPattern = '(?ms)^\[mcp_servers\.unreal\]\s*\r?\n.*?(?=^\[[^\]]+\]\s*$|\z)'
    $envPattern = '(?ms)^\[mcp_servers\.unreal\.env\]\s*\r?\n.*?(?=^\[[^\]]+\]\s*$|\z)'
    $text = [regex]::Replace($text, $sectionPattern, "")
    $text = [regex]::Replace($text, $envPattern, "")
    $text = $text.TrimEnd()

    $block = @"

[mcp_servers.unreal]
command = '$PythonPath'
args = ['$LauncherPath']

[mcp_servers.unreal.env]
UE_CONNECT_HOST = "127.0.0.1"
UE_CONNECT_MODE = "auto"
"@
    Write-Utf8 $configPath ($text + $block.TrimEnd() + "`r`n")
    return $configPath
}

function Register-CodexUnrealMcp([string]$PythonPath, [string]$LauncherPath) {
    $codexCommand = Get-Command codex -ErrorAction SilentlyContinue
    $codexPath = if ($codexCommand) {
        $codexCommand.Source
    } else {
        Join-Path $env:LOCALAPPDATA "OpenAI\Codex\bin\codex.exe"
    }
    if (-not (Test-Path -LiteralPath $codexPath)) {
        throw "Codex CLI was not found. Install Codex Desktop first; the script refuses to claim success without refreshing the MCP registry."
    }

    Write-Step "Refreshing Codex's MCP registry entry"
    & $codexPath mcp remove unreal *> $null
    & $codexPath mcp add unreal `
        --env "UE_CONNECT_HOST=127.0.0.1" `
        --env "UE_CONNECT_MODE=auto" `
        -- $PythonPath $LauncherPath
    if ($LASTEXITCODE -ne 0) {
        throw "codex mcp add failed. The config file was updated, but the MCP registry is not synchronized."
    }

    $entry = (& $codexPath mcp get unreal | Out-String)
    if ($LASTEXITCODE -ne 0 -or
        $entry -notmatch [regex]::Escape($PythonPath) -or
        $entry -notmatch [regex]::Escape($LauncherPath)) {
        throw "Codex MCP registry verification failed: unreal does not point to the generated global launcher."
    }
    return $true
}

function Enable-ProjectRemoteExecution([string]$ProjectPath) {
    if ([string]::IsNullOrWhiteSpace($ProjectPath)) { return $null }

    $resolved = (Resolve-Path -LiteralPath $ProjectPath).Path
    if ((Get-Item -LiteralPath $resolved).PSIsContainer) {
        $projectDir = $resolved
    } else {
        $projectDir = Split-Path -Parent $resolved
    }

    $iniPath = Join-Path $projectDir "Config\DefaultEngine.ini"
    if (-not (Test-Path -LiteralPath $iniPath)) {
        throw "DefaultEngine.ini was not found at $iniPath"
    }

    $text = [System.IO.File]::ReadAllText($iniPath)
    Copy-Item -LiteralPath $iniPath -Destination ($iniPath + ".before-unreal-mcp") -Force
    $header = '[/Script/PythonScriptPlugin.PythonScriptPluginSettings]'
    $sectionPattern = '(?ms)^\[/Script/PythonScriptPlugin\.PythonScriptPluginSettings\]\s*\r?\n.*?(?=^\[[^\]]+\]\s*$|\z)'
    $match = [regex]::Match($text, $sectionPattern)
    if ($match.Success) {
        $section = $match.Value
        if ($section -match '(?m)^bRemoteExecution\s*=') {
            $section = [regex]::Replace($section, '(?m)^bRemoteExecution\s*=.*$', 'bRemoteExecution=True')
        } else {
            $section = $section.TrimEnd() + "`r`nbRemoteExecution=True`r`n"
        }
        $text = $text.Remove($match.Index, $match.Length).Insert($match.Index, $section)
    } else {
        $text = $text.TrimEnd() + "`r`n`r`n$header`r`nbRemoteExecution=True`r`n"
    }
    Write-Utf8 $iniPath $text
    return $iniPath
}

function Enable-PythonEditorPlugin([string]$ProjectPath) {
    if ([string]::IsNullOrWhiteSpace($ProjectPath)) { return $null }

    $resolved = (Resolve-Path -LiteralPath $ProjectPath).Path
    if ((Get-Item -LiteralPath $resolved).PSIsContainer) {
        $uprojects = @(Get-ChildItem -LiteralPath $resolved -Filter *.uproject -File)
        if ($uprojects.Count -ne 1) {
            throw "Pass the exact .uproject path when the directory does not contain exactly one .uproject file."
        }
        $uprojectPath = $uprojects[0].FullName
    } else {
        $uprojectPath = $resolved
    }

    if ([IO.Path]::GetExtension($uprojectPath) -ne ".uproject") {
        throw "UEProjectPath must be a UE project directory or a .uproject file."
    }

    $json = (Get-Content -Raw -LiteralPath $uprojectPath) | ConvertFrom-Json
    $plugins = @($json.Plugins)
    $pythonPlugin = $plugins | Where-Object { $_.Name -eq "PythonScriptPlugin" } | Select-Object -First 1
    if ($null -eq $pythonPlugin) {
        $pythonPlugin = [pscustomobject]@{
            Name = "PythonScriptPlugin"
            Enabled = $true
            TargetAllowList = @("Editor")
        }
        $plugins += $pythonPlugin
    } else {
        $pythonPlugin.Enabled = $true
        if ($null -eq $pythonPlugin.TargetAllowList) {
            $pythonPlugin | Add-Member -NotePropertyName TargetAllowList -NotePropertyValue @("Editor")
        }
    }
    $json.Plugins = $plugins
    Copy-Item -LiteralPath $uprojectPath -Destination ($uprojectPath + ".before-unreal-mcp") -Force
    Write-Utf8 $uprojectPath (($json | ConvertTo-Json -Depth 50) + "`r`n")
    return $uprojectPath
}

function Test-UnrealMcp([string]$PythonPath, [string]$LauncherPath) {
    $probePath = Join-Path $env:TEMP ("codex_unreal_mcp_probe_" + [guid]::NewGuid().ToString("N") + ".py")
    $probe = @'
import asyncio
import json
import os
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

async def main():
    python_path = os.environ["UNREAL_MCP_PROBE_PYTHON"]
    launcher_path = os.environ["UNREAL_MCP_PROBE_LAUNCHER"]
    params = StdioServerParameters(
        command=python_path,
        args=[launcher_path],
        env=dict(os.environ, UE_CONNECT_HOST="127.0.0.1", UE_CONNECT_MODE="auto"),
    )
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            tools = await session.list_tools()
            tool_names = [tool.name for tool in tools.tools]
            result = await session.call_tool("ping", arguments={})
            payload = result.content[0].text
            status = json.loads(payload)
            print(json.dumps({
                "guide_tool": "read_usage_guide" in tool_names,
                "ping": status,
            }, ensure_ascii=False))
            raise SystemExit(0 if status.get("connected") else 2)

asyncio.run(main())
'@
    Write-Utf8 $probePath $probe
    $oldPython = $env:UNREAL_MCP_PROBE_PYTHON
    $oldLauncher = $env:UNREAL_MCP_PROBE_LAUNCHER
    try {
        $env:UNREAL_MCP_PROBE_PYTHON = $PythonPath
        $env:UNREAL_MCP_PROBE_LAUNCHER = $LauncherPath
        & $PythonPath -B $probePath
        $code = $LASTEXITCODE
        if ($code -eq 2) {
            Write-Warning "MCP is installed, but UE5 did not report connected. Start UE5 with the target project and retry the ping."
        } elseif ($code -ne 0) {
            throw "The Unreal MCP self-test failed with exit code $code."
        }
    } finally {
        if ($null -eq $oldPython) { Remove-Item Env:UNREAL_MCP_PROBE_PYTHON -ErrorAction SilentlyContinue } else { $env:UNREAL_MCP_PROBE_PYTHON = $oldPython }
        if ($null -eq $oldLauncher) { Remove-Item Env:UNREAL_MCP_PROBE_LAUNCHER -ErrorAction SilentlyContinue } else { $env:UNREAL_MCP_PROBE_LAUNCHER = $oldLauncher }
        Remove-Item -LiteralPath $probePath -Force -ErrorAction SilentlyContinue
    }
}

$uv = Resolve-Uv
$python = Install-UnrealMcp $uv $UnrealMcpSource
$globalDir = Join-Path $env:USERPROFILE ".codex\mcp\unreal"
New-Item -ItemType Directory -Force -Path $globalDir | Out-Null

$launcher = @'
"""MCP stdio proxy backed by one shared Unreal connection per machine."""

from __future__ import annotations

import asyncio
import json
import os
import socket
import subprocess
import sys
from pathlib import Path
from typing import Any

from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import ImageContent, Resource, TextContent, Tool

HOST = "127.0.0.1"
PORT = int(os.environ.get("UE_MCP_BACKEND_PORT", "38567"))
BACKEND_SCRIPT = Path(__file__).with_name("unreal_mcp_backend.py")
GUIDE_URI = "unreal://mcp/usage"
GUIDE_PATH = Path(__file__).with_name("README.md")
BACKEND_LOG = Path(os.environ.get("TEMP", ".")) / "codex_unreal_mcp_backend.log"

def _launcher_image() -> str:
    if os.name == "nt":
        import ctypes
        parent_pid = os.getppid()
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        handle = kernel32.OpenProcess(0x1000, False, parent_pid)
        if handle:
            try:
                size = ctypes.c_uint32(32768)
                buffer = ctypes.create_unicode_buffer(size.value)
                if kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
                    image = buffer.value
                    if "unreal-mcp" in image.casefold():
                        return image
            finally:
                kernel32.CloseHandle(handle)
    return sys.executable

def _can_connect() -> bool:
    try:
        with socket.create_connection((HOST, PORT), timeout=0.25):
            return True
    except OSError:
        return False

def _start_backend() -> None:
    if _can_connect():
        return
    BACKEND_LOG.parent.mkdir(parents=True, exist_ok=True)
    log_stream = BACKEND_LOG.open("ab")
    flags = (
        getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
        | getattr(subprocess, "DETACHED_PROCESS", 0)
        | getattr(subprocess, "CREATE_BREAKAWAY_FROM_JOB", 0x01000000)
    )
    try:
        subprocess.Popen(
            [_launcher_image(), str(BACKEND_SCRIPT)],
            stdin=subprocess.DEVNULL,
            stdout=log_stream,
            stderr=subprocess.STDOUT,
            close_fds=True,
            creationflags=flags,
            env=dict(os.environ),
        )
    finally:
        log_stream.close()

async def _ensure_backend() -> None:
    await asyncio.to_thread(_start_backend)
    for _ in range(80):
        if await asyncio.to_thread(_can_connect):
            return
        await asyncio.sleep(0.1)
    raise RuntimeError("Shared Unreal MCP backend did not start")

async def _rpc(method: str, params: dict[str, Any]) -> dict[str, Any]:
    reader, writer = await asyncio.open_connection(HOST, PORT)
    try:
        request = {"id": id(params), "method": method, "params": params}
        writer.write((json.dumps(request, ensure_ascii=False) + "\n").encode("utf-8"))
        await writer.drain()
        response = json.loads((await reader.readline()).decode("utf-8"))
        if not response.get("ok"):
            raise RuntimeError(response.get("error", "Shared backend request failed"))
        return response["result"]
    finally:
        writer.close()
        await writer.wait_closed()

app = Server("unreal-mcp")

@app.list_tools()
async def list_tools() -> list[Tool]:
    result = await _rpc("list_tools", {})
    tools = [Tool.model_validate(item) for item in result["tools"]]
    tools.append(Tool.model_validate({
        "name": "read_usage_guide",
        "description": "Read the global Unreal MCP operating guide before using UE5 tools.",
        "inputSchema": {"type": "object", "properties": {}},
    }))
    return tools

@app.call_tool()
async def call_tool(name: str, arguments: dict[str, Any]) -> list[TextContent | ImageContent]:
    if name == "read_usage_guide":
        return [TextContent(type="text", text=GUIDE_PATH.read_text(encoding="utf-8"))]
    result = await _rpc("call_tool", {"name": name, "arguments": arguments})
    content: list[TextContent | ImageContent] = []
    for item in result.get("content", []):
        if item.get("type") == "image":
            content.append(ImageContent.model_validate(item))
        else:
            content.append(TextContent.model_validate(item))
    return content

@app.list_resources()
async def list_resources() -> list[Resource]:
    result = await _rpc("list_resources", {})
    resources = [Resource.model_validate(item) for item in result["resources"]]
    resources.append(Resource.model_validate({
        "uri": GUIDE_URI,
        "name": "Unreal MCP Operating Guide",
        "description": "Global instructions for using Unreal MCP tools; independent of any UE5 project.",
        "mimeType": "text/markdown",
    }))
    return resources

@app.read_resource()
async def read_resource(uri: Any) -> str:
    if str(uri) == GUIDE_URI:
        return GUIDE_PATH.read_text(encoding="utf-8")
    result = await _rpc("read_resource", {"uri": str(uri)})
    return result["text"]

async def _run() -> None:
    await _ensure_backend()
    async with stdio_server() as streams:
        await app.run(streams[0], streams[1], app.create_initialization_options())

def main() -> None:
    asyncio.run(_run())

if __name__ == "__main__":
    main()
'@

$backend = @'
"""Shared local Unreal MCP backend."""

from __future__ import annotations

import asyncio
import json
import logging
import os
from pathlib import Path
from typing import Any

os.environ["UE_CONNECT_MODE"] = "direct"
os.environ["UE_CONNECT_HOST"] = "127.0.0.1"
os.environ["UE_COMMAND_HOST"] = "127.0.0.1"

from unreal_mcp import server as unreal_server
from unreal_mcp.connection import ConnectionState, get_connection
from unreal_mcp.reconnect import run_reconnect_loop

HOST = "127.0.0.1"
PORT = int(os.environ.get("UE_MCP_BACKEND_PORT", "38567"))
CALL_LOCK = asyncio.Lock()
PID_FILE = Path(os.environ.get("TEMP", ".")) / "codex_unreal_mcp_backend.pid"

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("unreal_mcp_backend")

def _model_json(value: Any) -> dict[str, Any]:
    if hasattr(value, "model_dump"):
        return value.model_dump(mode="json")
    return value

async def _dispatch(method: str, params: dict[str, Any]) -> Any:
    if method == "list_tools":
        tools = await unreal_server.list_tools()
        return {"tools": [_model_json(tool) for tool in tools]}
    if method == "call_tool":
        async with CALL_LOCK:
            content = await unreal_server.call_tool(str(params.get("name", "")), params.get("arguments") or {})
        return {"content": [_model_json(item) for item in content]}
    if method == "list_resources":
        resources = await unreal_server.list_resources()
        return {"resources": [_model_json(resource) for resource in resources]}
    if method == "read_resource":
        value = await unreal_server.read_resource(str(params.get("uri", "")))
        return {"text": value}
    raise ValueError(f"Unknown backend method: {method}")

async def _serve_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    try:
        while True:
            line = await reader.readline()
            if not line:
                return
            try:
                request = json.loads(line.decode("utf-8"))
                result = await _dispatch(request["method"], request.get("params") or {})
                response = {"id": request.get("id"), "ok": True, "result": result}
            except Exception as exc:
                response = {"id": request.get("id") if "request" in locals() else None, "ok": False, "error": str(exc)}
            writer.write((json.dumps(response, ensure_ascii=False) + "\n").encode("utf-8"))
            await writer.drain()
    finally:
        writer.close()
        await writer.wait_closed()

async def main() -> None:
    conn = get_connection()
    conn.state = ConnectionState.CONNECTING
    conn.push_ue_status = lambda *_args, **_kwargs: None
    reconnect_task = asyncio.create_task(run_reconnect_loop(conn, on_connect=lambda _conn: None))
    tcp_server = await asyncio.start_server(_serve_client, HOST, PORT)
    root_pid = os.getppid()
    PID_FILE.write_text(str(root_pid), encoding="ascii")
    logger.info("Shared Unreal MCP backend listening on %s:%d", HOST, PORT)
    try:
        async with tcp_server:
            await tcp_server.serve_forever()
    finally:
        reconnect_task.cancel()
        try:
            await reconnect_task
        except asyncio.CancelledError:
            pass
        conn.disconnect()
        try:
            if PID_FILE.read_text(encoding="ascii").strip() == str(root_pid):
                PID_FILE.unlink(missing_ok=True)
        except (FileNotFoundError, OSError):
            pass

if __name__ == "__main__":
    asyncio.run(main())
'@

$readme = @'
# Unreal MCP 全局配置

本目录由 setup_unreal_mcp.ps1 创建，属于用户级 Codex 配置，不属于任何 UE5 项目。

文件：

- unreal_mcp_launcher.py：Codex MCP stdio 入口。
- unreal_mcp_backend.py：共享后端，保证同一台机器只有一个 UE5 Remote Execution 连接。
- README.md：给 AI 读取的全局操作说明。

AI 应先调用 `mcp__unreal__read_usage_guide`，也可以读取 MCP 资源 `unreal://mcp/usage`。

运行链路：Codex → launcher → 127.0.0.1:38567 → backend → unreal_mcp Python 包 → UE5 Remote Execution。

当前安装的原始 `unreal_mcp` 包由 uv 管理，不要复制到项目目录，也不要手动启动多个 backend。安装脚本会同时更新 `config.toml`，并通过 `codex mcp remove/add` 刷新 Codex 的实际注册项。

UE5 目标项目需要启用：

```ini
[/Script/PythonScriptPlugin.PythonScriptPluginSettings]
bRemoteExecution=True
```

验证：启动 UE5 并打开目标项目后，在 Codex 中调用 `mcp__unreal__ping`。
'@

Write-Step "Writing the global shared Unreal MCP bridge"
Write-Utf8 (Join-Path $globalDir "unreal_mcp_launcher.py") $launcher
Write-Utf8 (Join-Path $globalDir "unreal_mcp_backend.py") $backend
Write-Utf8 (Join-Path $globalDir "README.md") $readme

$config = Set-CodexUnrealMcpConfig $python (Join-Path $globalDir "unreal_mcp_launcher.py")
Write-Host "Codex config: $config" -ForegroundColor Green
Write-Host "Unreal MCP bridge: $globalDir" -ForegroundColor Green
$registryUpdated = Register-CodexUnrealMcp $python (Join-Path $globalDir "unreal_mcp_launcher.py")
if ($registryUpdated) {
    Write-Host "Codex MCP registry: unreal -> global bridge" -ForegroundColor Green
}

if (-not [string]::IsNullOrWhiteSpace($UEProjectPath)) {
    $uproject = Enable-PythonEditorPlugin $UEProjectPath
    $ini = Enable-ProjectRemoteExecution $UEProjectPath
    Write-Host "PythonScriptPlugin enabled in: $uproject" -ForegroundColor Green
    Write-Host "Remote Execution enabled in: $ini" -ForegroundColor Green
} else {
    Write-Warning "No -UEProjectPath was supplied. Enable Python Remote Execution in the target UE5 project before testing."
}

if (-not $SkipConnectionTest) {
    $editor = Get-Process -Name UnrealEditor -ErrorAction SilentlyContinue
    if ($editor) {
        Write-Step "Testing the global Unreal MCP entrypoint"
        Test-UnrealMcp $python (Join-Path $globalDir "unreal_mcp_launcher.py")
    } else {
        Write-Warning "UnrealEditor is not running, so the UE connection test was skipped. Installation/configuration is complete."
    }
}

Write-Host "`nSetup complete. Restart Codex once, then use mcp__unreal__ping in a new task." -ForegroundColor Green
