@echo off
rem Copy beside chrome.exe in the ReactOS test installation.
rem This experimental launch disables the sandbox and uses software rendering.
setlocal
set "CHROME_TARGET=%~1"
if not defined CHROME_TARGET set "CHROME_TARGET=%~dp0test.html"
"%~dp0chrome.exe" --user-data-dir="%~dp0reactos-render-profile" --no-first-run --no-default-browser-check --no-sandbox --disable-gpu --disable-gpu-compositing --disable-software-rasterizer --disable-features=RemoveRedirectionBitmap,RequestHeaderIntegrity --disable-background-networking --window-size=760,520 --window-position=10,10 "%CHROME_TARGET%"
