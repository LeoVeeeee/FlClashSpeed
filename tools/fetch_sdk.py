from pathlib import Path
import requests

root = Path(__file__).resolve().parents[1]
files = {
    "docs/插件开发指南.md": "https://raw.githubusercontent.com/wiki/zhongyang219/TrafficMonitor/插件开发指南.md",
    "sdk/PluginInterface.h": "https://raw.githubusercontent.com/zhongyang219/TrafficMonitor/V1.86/include/PluginInterface.h",
    "docs/PluginDemo.cpp": "https://raw.githubusercontent.com/zhongyang219/TrafficMonitor/master/PluginDemo/PluginDemo.cpp",
    "docs/PluginManager.cpp": "https://raw.githubusercontent.com/zhongyang219/TrafficMonitor/master/TrafficMonitor/PluginManager.cpp",
    "sdk/LICENSE.TrafficMonitor": "https://raw.githubusercontent.com/zhongyang219/TrafficMonitor/master/LICENSE",
}
for target, url in files.items():
    response = requests.get(url, timeout=30)
    print(target, response.status_code)
    if response.status_code != 200:
        continue
    path = root / target
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(response.content)
    if target.endswith("PluginInterface.h"):
        print(response.content.decode("utf-8-sig"))
