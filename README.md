# MAME4droid-Current

![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)
![Platform: Android](https://img.shields.io/badge/Platform-Android-brightgreen.svg)

**MAME4droid (Current)** is a powerful Android port of the **MAME 0.287** emulator (by MAMEDev and contributors), developed by **David Valdeita (Seleuco)**. It supports over 40,000 different ROMs and emulates classic arcade systems along with home computers like ZX Spectrum, Amstrad CPC, and MSX.

---

## ⚠️ Important Disclaimers

* **No ROMs Included**: MAME4droid is strictly an EMULATOR and DOES NOT INCLUDE ROMS or copyrighted material.
* **Not Affiliated with MAMEDev**: This project is independent of the official MAME team.
* **High-End Hardware Recommended**: Based on the latest PC MAME version, it requires high specifications. Modern games (90s+) may not run at full speed on all devices.
* **Support**: Due to the massive library of titles, support for specific individual games cannot be provided.

---

## 🚀 Performance & Hardware

While MAME is known for being demanding, this version includes modern optimizations for mobile hardware:

* **arm64 DRC (Dynamic Recompiler)**: Recent versions now feature a dedicated **64-bit ARMv8 recompiler (DRC)**. This provides a massive performance boost for complex systems (like Killer Instinct or CPS-3), allowing them to run much faster than in older, legacy emulators.
* **Requirements**: Despite these optimizations, a **high-end Android device** is still recommended for the most demanding 90s-era 3D arcade games to achieve full speed.

---

## 🛠️ Recommended Setup (External Storage)

For the best experience and to save internal storage, it is recommended to store your ROMs on external storage using Android's **Scoped Storage** system.

### First-Time Installation Guide
1.  **Prepare your external storage**: Create a folder named `MAME4droid` (or any name you prefer) on your SD card or internal memory.
2.  **Initial Launch**: Open the app for the first time.
3.  **Path Selection**: When asked "Where do you have stored or want to store your roms files?", select **EXTERNAL STORAGE**.
4.  **Grant Permission**:
    * The Android file picker will open. Navigate to your external storage, select the folder you created, and tap **"Use this folder"**.
    * Tap **Allow** to give MAME4droid permission to access that directory.
5.  **Romset Version**: Ensure you are using the **'0.287'** romset for compatibility.

*Note: If you already installed it, you can change the path in **Options > Settings > General > Change ROMs path**.*

---

## ✨ Features

* **🎮 Controls**: Plug-and-play support for Bluetooth/USB gamepads, touch lightgun, and mouse support.
* **📱 Interface**: Autorotation, customizable button layouts (1-6 buttons), and tilt sensor support.
* **🎨 Visuals & Shaders**:
    * **Shader Support**: This version includes advanced shader support for a more authentic retro look.
    * **How to Enable**: Shaders must be manually activated. Go to **Options > Settings > Shaders** to select and enable your preferred effects.
    * **Filters**: Includes overlay filters like scanlines and CRT effects.

---

## 📜 MAME License

Copyright (C) 1997-2026 MAMEDev and contributors.

This program is free software; you can redistribute it and/or modify it under the terms of the **GNU General Public License** as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.