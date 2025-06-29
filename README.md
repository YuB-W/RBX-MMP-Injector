# YuB-X MMP Injector with CFG Bypass

> version-78712d8739f34cb9

> 🚀 injector with full Control Flow Guard (CFG) bypass  
---

## 🛠 Features

- ✅ Manual Mapping Injection  
- 🔄 Control Flow Guard (CFG) Bypass  
  - CFG Bitmap manipulation  
  - Whitelist registration via `set_insert`, `cfg_cache`  
- 🔐 Hyperion/Byfron Protection Bypass  

## ⚙️ How It Works

1. Bypasses CFG using internal `cfg_cache` and `set_insert` mechanisms  
2. Whitelists allocated memory via direct bitmap patch or `sub_cbaf00` call  
---

## 💻 Compatibility

- ✅ Windows 10 / 11 (21H2–24H2+)  
- ❗ Requires valid CFG offsets (update as needed)  

---

## 🧬 Requirements

- Updated offsets:  
  - `cfg_cachee`  
  - `set_insert`  
  - `Offset_WhitelistPages`  

## ⚠️ Legal Disclaimer

This project is for **educational and research purposes only**.  
Using this on systems you don’t own or have permission to access may violate laws.  
By using this, you agree that you are solely responsible for any actions taken.

---

## YuB-X API
https://github.com/YuB-W/YuB-X-roblox-api


## 💬 Credits

- 🧠 YuB-X — [https://yub-x.com](https://yub-x.com)  
- 👥 Community researchers  
---

> 🧷 Discord: [https://discord.gg/4BPuyNkGsc](https://discord.gg/4BPuyNkGsc)  
> 🌐 Website: [https://yub-x.com](https://yub-x.com)
