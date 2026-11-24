<div align="center">

<img src="docs/chitti.png" alt="Chitti" width="230">

# Chitti: a Telugu AI friend that lives on a tiny round screen

[![Release](https://img.shields.io/github/v/release/chaitanyaeshwarprasad/chitti?style=flat-square&color=7c3aed)](https://github.com/chaitanyaeshwarprasad/chitti/releases/latest)
[![MIT License](https://img.shields.io/badge/License-MIT-22c55e?style=flat-square)](LICENSE)
[![Windows 10 | 11](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D6?style=flat-square&logo=windows&logoColor=white)](#3-what-you-need)
[![ESP32-S3](https://img.shields.io/badge/ESP32--S3-AMOLED%201.75C-E7352C?style=flat-square&logo=espressif&logoColor=white)](#1-the-hardware-i-used)
[![Claude](https://img.shields.io/badge/Brain-Claude-D97757?style=flat-square&logo=anthropic&logoColor=white)](#4-buy-your-api-keys-you-must-use-your-own)
[![Sarvam AI](https://img.shields.io/badge/Voice-Sarvam%20AI-2563eb?style=flat-square)](#4-buy-your-api-keys-you-must-use-your-own)
[![Telugu](https://img.shields.io/badge/Language-Telugu-f59e0b?style=flat-square)](#6-using-chitti)

</div>

Chitti is a talking AI companion. Tap her screen, speak in Telugu, English or a mix, and she answers out loud in natural Telugu with English subtitles, a face that reacts, and a personality you choose: a friendly roaster, a funny one, a chill one or a caring one.

She runs on a small round AMOLED board. The board connects over your Wi-Fi to a free program on your Windows PC. That program uses **your own** Claude and Sarvam API keys.

> [!TIP]
> **Free and open source.** You only pay for your own API use, usually a few rupees a day.<br>
> **Created by [Chaitanya Eshwar Prasad](https://chaitanyaeshwarprasad.com)**. Watch the setup video and more on [YouTube](https://www.youtube.com/@chaitanya.eshwar.prasad) and [Instagram @acep.tech.in.telugu](https://instagram.com/acep.tech.in.telugu).<br>
> **Want to build one yourself?** [Chitti Junior](https://github.com/chaitanyaeshwarprasad/chitti-junior) is the same Chitti, assembled from a few common parts.

<div align="center">

[![Website](https://img.shields.io/badge/Website-chaitanyaeshwarprasad.com-005F9E?style=for-the-badge&logo=google-chrome&logoColor=white)](https://chaitanyaeshwarprasad.com)
[![GitHub](https://img.shields.io/badge/GitHub-chaitanyaeshwarprasad-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/chaitanyaeshwarprasad)
[![LinkedIn](https://img.shields.io/badge/LinkedIn-Chaitanya%20Eshwar%20Prasad-0A66C2?style=for-the-badge&logo=linkedin&logoColor=white)](https://linkedin.com/in/chaitanya-eshwar-prasad)
[![YouTube](https://img.shields.io/badge/YouTube-@chaitanya.eshwar.prasad-FF0000?style=for-the-badge&logo=youtube&logoColor=white)](https://www.youtube.com/@chaitanya.eshwar.prasad)
[![Instagram](https://img.shields.io/badge/Instagram-@acep.tech.in.telugu-E4405F?style=for-the-badge&logo=instagram&logoColor=white)](https://instagram.com/acep.tech.in.telugu)

</div>

---

## Contents

1. [The hardware I used](#1-the-hardware-i-used)
2. [How Chitti works](#2-how-chitti-works)
3. [What you need](#3-what-you-need)
4. [Buy your API keys](#4-buy-your-api-keys-you-must-use-your-own)
5. [Install, step by step](#5-install-step-by-step)
6. [Using Chitti](#6-using-chitti)
7. [The dashboard](#7-the-dashboard)
8. [Privacy and safety](#8-privacy-and-safety)
9. [Troubleshooting](#9-troubleshooting)
10. [For developers](#10-for-developers)
11. [Disclaimer](#11-disclaimer-please-read)
12. [Credits and licence](#12-credits-and-licence)
13. [About the author](#-about-the-author)

---

## 1. The hardware I used

I built Chitti on this exact board:

**Waveshare ESP32-S3-Touch-AMOLED-1.75C**
https://www.waveshare.com/product/esp32-related/displays/amoled/esp32-s3-touch-amoled-1.75c.htm

| Part | What it is | What Chitti uses it for |
|---|---|---|
| Chip | ESP32-S3: dual core, 240 MHz, Wi-Fi 2.4 GHz and Bluetooth | Runs Chitti's face, mic, speaker and Wi-Fi |
| Memory | 8 MB PSRAM and 32 MB flash | Animations and voice buffering |
| Screen | 1.75 inch round AMOLED, 466 × 466, capacitive touch | Her face, subtitles and touch gestures |
| Microphones | Two mics with an ES7210 audio chip | Hears you, with noise reduction |
| Speaker | ES8311 audio chip with an on-board amplifier and speaker connector | Her voice |
| Motion sensor | QMI8658 6-axis sensor | Shake, pick-up and wake gestures |
| Power | AXP2101 power chip, LiPo battery connector, USB-C | Battery level, charging, sleep |

**Where to buy:** from the Waveshare product page above, or from Waveshare's official sellers. Check that the listing says **1.75C**.

**Buying tips**
- Buy the **1.75C** version: round screen, with mics and speaker. Other versions have different pins, and this firmware will **not** work on them.
- Get the version **with the speaker and a case** if available. Add a small **3.7 V LiPo battery** if you want her cordless; she also works from USB alone.
- You need a **USB-C data cable**. Some cables only charge, and flashing won't work with them.

Pin details are in [docs/hardware.md](docs/hardware.md).

---

## 2. How Chitti works

```
  You speak ─► Chitti (board) ──Wi-Fi──► Your PC (Chitti server) ──Internet──► Claude (brain)
                    ▲                          │                       └────► Sarvam (Telugu voice)
                    └──────── her voice ◄──────┘
```

1. **You tap her screen and talk.** The board records your voice and streams it over your home Wi-Fi to your PC.
2. **Your PC does the thinking.** The Chitti server running on your PC:
   - turns your voice into text with **Sarvam** speech-to-text (or **Whisper**, which runs free on your PC if you have no Sarvam key);
   - asks **Claude** (Anthropic) for a reply in Chitti's personality and Telugu style. She remembers your name and facts about you, on your PC;
   - turns the reply into Telugu speech with **Sarvam** text-to-speech (or a free Microsoft voice).
3. **She answers** through her speaker, with subtitles and a matching face.

**How the board finds your PC.** You never type an IP address. When Chitti starts, she sends a "where are you?" message on your Wi-Fi, and the server on your PC answers. Both must be on the **same Wi-Fi network**.

| Port on your PC | Used for |
|---|---|
| 8765 (TCP, WebSocket) | Voice and replies between Chitti and the PC |
| 8766 (UDP) | Auto-discovery: the board finds the PC |
| 8768 (HTTP, this PC only) | The dashboard at http://localhost:8768 |

> [!NOTE]
> Windows asks once to allow Python through the firewall. Allow it on **private networks**.

---

## 3. What you need

| Item | Notes |
|---|---|
| The board | See [section 1](#1-the-hardware-i-used) |
| USB-C data cable | For flashing and Wi-Fi setup |
| Windows 10 or 11 PC or laptop | Stays on (not asleep) while you talk to Chitti. Wi-Fi or a LAN cable both work. |
| Python 3.12 | **Installed for you** if missing. Or get it from https://www.python.org/downloads/release/python-3129/ and tick **"Add python.exe to PATH"**. Python 3.13 and newer are not supported yet. |
| ffmpeg, Microsoft Visual C++ runtime | **Installed for you** if missing |
| About 4 GB free disk space and internet | Checked by the installer |
| 2.4 GHz Wi-Fi | The ESP32 does not support 5 GHz. A phone hotspot works too, if the PC joins the same hotspot. |
| **Your own Anthropic API key** | Required. See below. |
| Your own Sarvam API key | Optional but recommended, for natural Telugu voice and better hearing |

---

## 4. Buy your API keys (you must use your own)

Chitti does **not** come with any API keys. Every user creates their own and pays for their own usage. Nobody else's key is in this project, and **you should never share yours**.

### Anthropic (Claude): required. Chitti's brain.
1. Go to https://console.anthropic.com and sign up.
2. Open **Billing** and add credits. A small amount like $5 lasts a long time with the Haiku model.
3. **Set a monthly spend limit** under **Limits**, so you can never be charged more than you choose.
4. Open **API Keys**, then **Create Key**, and copy it. It starts with `sk-ant-`.

### Sarvam AI: optional. Natural Telugu voice and hearing.
1. Go to https://dashboard.sarvam.ai and sign up. New accounts usually get some free credits.
2. Create an API key and copy it.

Without a Sarvam key Chitti still works: she uses a free Microsoft Telugu voice and hears you with Whisper on your PC. Whisper is slower and less accurate for Telugu.

### What it costs
Prices change, so always check the official pricing pages. As a rough guide:
- **Claude Haiku 5.5** (default) costs a fraction of a rupee per reply. **Sonnet 5.5** is smarter but costs several times more.
- **Sarvam** speech-to-text costs about ₹0.5 per minute of talking. Text-to-speech costs about ₹10–30 per 10,000 characters, depending on the voice.
- Chatting for a while every day usually costs **a few rupees a day**.

> [!WARNING]
> Use the spend limits on both websites. You are responsible for your own API bills.

---

## 5. Install, step by step

For a full guide with screenshots, see **[INSTALL.md](INSTALL.md)**.

1. **Download** this project: on GitHub, click **Code**, then **Download ZIP**, then unzip it. Use a simple folder such as `C:\Chitti`, with no OneDrive folder.
2. Double-click **`Start Chitti.bat`**. The first time, it checks your PC and installs everything Chitti needs (about 5–10 minutes), then opens the dashboard in your browser. Keep the black window open while you use Chitti.

   The installer checks each of these and fixes what's missing, asking you first:
   - 64-bit Windows, disk space and internet;
   - Python 3.12;
   - the Microsoft Visual C++ runtime;
   - Chitti's packages;
   - ffmpeg;
   - a Windows Firewall rule so Chitti's board can reach your PC.
3. The dashboard's **setup wizard** walks you through everything else:
   1. **Keys**: paste your Claude key, and a Sarvam key if you have one.
   2. **Install Chitti on the board**: plug the board in with USB-C and click **Install Chitti**. A progress bar shows the install, which takes about 1–2 minutes.
   3. **Wi-Fi**: pick your 2.4 GHz Wi-Fi and type the password. It is sent to the board through the cable.
   4. **Say hi**: when the wizard says **Chitti is online**, tap her screen and say *"హాయ్ చిట్టి!"*

> [!TIP]
> **Every day after that:** switch Chitti on, double-click `Start Chitti.bat`, and talk.

You can redo any step later: the dashboard's **Device** section has **Install firmware** and **Run setup again**.

The `tools` folder has manual versions for advanced users and for when the dashboard can't help:
- `Flash Chitti.bat`
- `Connect Chitti to Wi-Fi.bat`
- `Save API keys.bat`
- `Allow Chitti in Firewall.bat`
- `Open Dashboard.bat`


---

## 6. Using Chitti

| Do this | Chitti does |
|---|---|
| **Tap** the screen | Starts listening. Talk, then stop; she notices when you're done. |
| **Press and hold** | Poke: she reacts. |
| **Stroke** across the screen | Pat: she likes it. |
| **Swipe up or down** | Volume up or down. |
| **Shake** her | She laughs or gets dizzy. |
| Leave her alone | She dims, then sleeps. Touch or pick her up to wake her. |

Things to try: *"నన్ను రోస్ట్ చెయ్"* (roast me), *"ఈరోజు వాతావరణం ఎలా ఉంది?"* (weather), *"నా పేరు ___, గుర్తుపెట్టుకో"* (remember my name), *"రేపు 7 కి నాకు గుర్తు చెయ్"* (reminders).

---

## 7. The dashboard

Open **http://localhost:8768** on the PC running Chitti. It only works on that PC, not on other devices.

- **Setup wizard**: opens by itself the first time. It covers keys, installing Chitti on the board with a progress bar, Wi-Fi, then "say hi".
- **Home**: an animated Chitti that follows the real one (listening, thinking, speaking), her battery, her last reply, a chat box to type to her, and a **setup checklist** showing what's connected.
- **Settings**: your API keys, which are stored only on your PC; Haiku or Sonnet; her voice; personality (Roaster, Funny, Chill, Caring); Telugu style (Telgish, Hyderabadi, Godavari, Gen Z…); words she should or must never use; your city for weather.
- **Device**: speaker volume, Wi-Fi setup over USB, **Install firmware**, **Run setup again**, and **Reset**, which erases her memory of you.
- **Help**: how Chitti connects, and what to do when she doesn't.

---

## 8. Privacy and safety

- **Your keys stay on your PC**, in `server\.env` and your Windows user settings. They are only sent to their own service: Anthropic or Sarvam.
- **Your chats and memories stay on your PC** in the `server` folder: `memory.json`, `facts.json` and `reminders.json`. Reset in the dashboard erases them.
- **Nothing is sent to the author of Chitti.** There is no analytics, no tracking and no author server. The only services contacted:
  - **Anthropic**, which receives the conversation text to write replies;
  - **Sarvam**, which receives your voice and reply text, only if you add a Sarvam key;
  - **Microsoft Edge TTS**, which receives the reply text, only for the free voice;
  - **Open-Meteo**, which receives your city name, only when you ask about the weather.
- Read Anthropic's and Sarvam's own privacy policies to understand how they handle your data.
- The dashboard only accepts connections from your own PC.
- **If you fork or upload this project, never commit `server\.env`.** The included `.gitignore` already keeps keys and personal files out of git.
- Your Wi-Fi password is stored only on the board.

---

## 9. Troubleshooting

### Quick answers

- **Do I need a Wi-Fi adapter on my PC?** No. The PC just needs to be on the same router as Chitti, by Wi-Fi or by LAN cable. A laptop on Wi-Fi is perfect. You only need a Wi-Fi adapter to use the **Scan** button; without one, type the Wi-Fi name.
- **My router shows two names, like "Home" and "Home-5G".** Put Chitti on the one without "5G". The PC can use either, because both come from the same router.
- **Can I use guest Wi-Fi?** Usually no. Guest networks (and routers with "AP isolation") stop devices from seeing each other. Use your main Wi-Fi.
- **Does the PC need to stay on?** Yes. Chitti's brain runs on the PC. If the laptop sleeps or you close the lid, she goes offline until it wakes. Keep the black **Start Chitti** window open.
- **Does Chitti work without internet?** No. She needs the internet to reach Claude and Sarvam. If the internet drops, she says so out loud.

### Problems and fixes

| Problem | Fix |
|---|---|
| Dashboard says **Chitti offline** | Make sure the board and PC are on the **same 2.4 GHz Wi-Fi**. Send the Wi-Fi again: Device, then **Run setup again**. Allow Python in Windows Firewall (Windows Security, then Firewall, then Allow an app). Restart the board. |
| Install on board fails, or board not found | Try another USB-C cable (data, not charge-only). Close Arduino IDE or serial monitors. Hold **BOOT**, tap **RESET**, release **BOOT**, then try again. |
| "Python 3.12 is needed" | Install Python 3.12 with **Add python.exe to PATH** ticked, then double-click `Start Chitti.bat` again. |
| Install stopped halfway | Fix the message it shows, then double-click `Start Chitti.bat` again; it continues where it stopped. If it keeps failing, delete the `server\.venv` folder and try again. |
| Board never connects (firewall) | Run `tools\Allow Chitti in Firewall.bat`. |
| No voice / silent replies | Check that ffmpeg is installed: open a new window and type `ffmpeg -version`. Raise the volume in the dashboard. |
| "API key" errors | Check that your key is saved (green tick on Home), that you have credits, and that your spend limit isn't reached. |
| Wi-Fi scan shows nothing | Windows needs **Location services** turned on to scan. Or just type the Wi-Fi name. |
| She mishears Telugu | Add a Sarvam key. It is much better than Whisper for Telugu. Speak close to her. |
| Port already in use | Another program uses 8765, 8766 or 8768. Close it, or set `CHITTI_PORT`, `CHITTI_DASHBOARD_PORT`, `CHITTI_DISCOVERY_PORT` (see below). |

The black **Start Chitti** window shows what's happening. A log is also saved to `server\chitti.log`.

---

## 10. For developers

```
chitti/
├─ Start Chitti.bat       installs on first run, then starts Chitti and the dashboard
├─ tools/                 manual flash / Wi-Fi / key scripts
├─ server/                 PC server (Python)
│  ├─ chitti_server.py     WebSocket server: speech-to-text → Claude → text-to-speech
│  ├─ dashboard.py         local web dashboard
│  ├─ features.py          memory, facts, reminders, weather
│  ├─ slang_presets.py     personalities and Telugu styles
│  └─ set_wifi.py          sends Wi-Fi details to the board over USB
├─ firmware/               ESP-IDF v5.5 firmware source (C)
│  └─ prebuilt/            ready-made .bin files the dashboard installs
└─ docs/                   protocol.md (board ↔ PC messages), hardware.md (pins)
```

**Environment variables** (optional):
- `CHITTI_MODEL`
- `CHITTI_PORT` (8765), `CHITTI_DISCOVERY_PORT` (8766), `CHITTI_DASHBOARD_PORT` (8768)
- `CHITTI_NO_BROWSER=1`

The board finds the PC on UDP port 8766, so change it only if you also change the firmware.

**Building the firmware yourself.** Install ESP-IDF v5.5 (e.g. 5.5.x from Espressif's Windows installer). Put its `esp-idf` folder path in `firmware\idf_path.txt`, then run:

```bat
firmware\build.bat flash
```

The animation data is flashed with `firmware\build.bat anim`. Check prebuilt files against `firmware\prebuilt\SHA256SUMS.txt`.

**Flash layout:**

| Offset | File |
|---|---|
| `0x0` | `bootloader.bin` |
| `0x8000` | `partition-table.bin` |
| `0x10000` | `chitti.bin` |
| `0x610000` | `anim.bin` |

Flash settings: DIO, 80 MHz, 32 MB.

---

## 11. Disclaimer (please read)

> [!IMPORTANT]
> This project is shared **free, for learning and fun, "as is", with no warranty of any kind.** By downloading, flashing or using it, you agree that:

- **The author (Chaitanya Eshwar Prasad) is not responsible for anything** that happens from using this project. That includes:
  - damaged or "bricked" boards, batteries, PCs or other hardware;
  - data loss, overheating, fire, injury or electrical problems;
  - **API charges or bills** from Anthropic, Sarvam or any other service;
  - leaked or misused API keys;
  - anything Chitti says, since AI replies can be wrong, rude or inappropriate;
  - any other loss or damage.
- **You use your own API keys and pay your own bills.** Set spend limits.
- **Batteries:** LiPo batteries can be dangerous. Use a proper protected battery, never leave it charging unattended, and stop if anything gets hot or swells.
- Flashing replaces the board's original software. Use the optional backup if you may want it back.
- Chitti is **not** a medical, legal, financial or safety tool, and is not meant for unsupervised young children.
- I am **not affiliated with** Waveshare, Espressif, Anthropic, Sarvam AI or Microsoft. All trademarks belong to their owners. The product link is just the board I used.
- You are responsible for following the terms of service of every API you use and the laws where you live.

---

## 12. Credits and licence

**Chitti was created by Chaitanya Eshwar Prasad.** If you make a video or a project with it, please credit **Chaitanya Eshwar Prasad** and link back to this repository.

Released under the [MIT Licence](LICENSE). Built with:
- Claude by Anthropic;
- Sarvam AI;
- ESP-IDF by Espressif;
- the Waveshare board drivers;
- faster-whisper, edge-tts and Open-Meteo.

---

## 👤 About the Author

<div align="center">

Created and maintained by **Chaitanya Eshwar Prasad**. Connect with me or follow my work:

[![Website](https://img.shields.io/badge/Website-chaitanyaeshwarprasad.com-005F9E?style=for-the-badge&logo=google-chrome&logoColor=white)](https://chaitanyaeshwarprasad.com)
[![GitHub](https://img.shields.io/badge/GitHub-chaitanyaeshwarprasad-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/chaitanyaeshwarprasad)
[![LinkedIn](https://img.shields.io/badge/LinkedIn-Chaitanya%20Eshwar%20Prasad-0A66C2?style=for-the-badge&logo=linkedin&logoColor=white)](https://linkedin.com/in/chaitanya-eshwar-prasad)
[![YouTube](https://img.shields.io/badge/YouTube-@chaitanya.eshwar.prasad-FF0000?style=for-the-badge&logo=youtube&logoColor=white)](https://www.youtube.com/@chaitanya.eshwar.prasad)
[![Instagram](https://img.shields.io/badge/Instagram-@acep.tech.in.telugu-E4405F?style=for-the-badge&logo=instagram&logoColor=white)](https://instagram.com/acep.tech.in.telugu)

</div>
