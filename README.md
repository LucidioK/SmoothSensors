# SmoothSensors02

## If you are using WSL (Windows System for Linux)

You need to allow WSL to share the Windows network stack.Open your Windows User profile folder with:

```powershell
notepad "$env:USERPROFILE\.wslconfig
```
Add or modify the file to include these lines:

```text
ini[wsl2]
networkingMode=mirrored
```

Save the file, then completely restart WSL by running this in PowerShell:

```powershell
wsl --shutdown
```

Once restarted, WSL will share the exact same network environment as Windows, allowing you to interact with the Arduino's network IP directly.

### Install other tools:

```sh
sudo apt update
sudo apt install nmap
sudo apt install jq

```

