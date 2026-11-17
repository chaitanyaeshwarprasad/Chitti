# Installing Chitti: the complete beginner guide

By **Chaitanya Eshwar Prasad**. This guide follows the setup video on [YouTube @chaitanya.eshwar.prasad](https://www.youtube.com/@chaitanya.eshwar.prasad) step by step. It takes about **15 minutes** the first time.

You need:
- the **Waveshare ESP32-S3-Touch-AMOLED-1.75C** board ([product page](https://www.waveshare.com/product/esp32-related/displays/amoled/esp32-s3-touch-amoled-1.75c.htm));
- a USB-C **data** cable;
- a Windows 10 or 11 PC or laptop (on Wi-Fi or a LAN cable; no extra Wi-Fi adapter needed);
- 2.4 GHz Wi-Fi.

---

## Step 0: Get your own API keys

Chitti uses **your** keys, and you pay only for your own use. Never share your keys with anyone, and never show them in a video or screenshot.

**Anthropic (required)**
1. Open https://console.anthropic.com and create an account.
2. Go to **Settings, then Billing**, and buy a small amount of credits, e.g. $5.
3. Go to **Settings, then Limits**, and set a **monthly spend limit** you are comfortable with.
4. Go to **API Keys, then Create Key**. Name it "Chitti" and copy the key (`sk-ant-...`). Keep it somewhere safe for the next steps.

**Sarvam AI (optional, recommended for Telugu)**
1. Open https://dashboard.sarvam.ai and create an account.
2. Create an **API key** and copy it.

> 📷 *screenshot: Anthropic console, API Keys page*

---

## Step 1: Python (usually automatic)

`Start Chitti.bat` installs Python 3.12 for you if it's missing. If it can't, it opens the download page:

1. Download **Python 3.12** from https://www.python.org/downloads/release/python-3129/. Choose "Windows installer (64-bit)".
2. Run it and **tick "Add python.exe to PATH"** at the bottom. Then click **Install Now**.

> Newer Python versions (3.13 and later) are not supported yet. 3.10–3.12 work.

> 📷 *screenshot: Python installer with "Add python.exe to PATH" ticked*

---

## Step 2: Download Chitti

1. On the GitHub page click the green **Code** button, then **Download ZIP**.
2. Right-click the ZIP, then **Extract All**, to a simple place like `C:\Chitti`.
   - Avoid OneDrive, Desktop sync folders and paths with special characters.

---

## Step 3: Start Chitti (`Start Chitti.bat`)

1. Open the Chitti folder and double-click **`Start Chitti.bat`**.
   - If Windows shows "Windows protected your PC", click **More info**, then **Run anyway**. It's a plain text script; you can open it in Notepad to read it first.
2. The first time, it checks your PC in 8 steps and installs what's missing. This takes about 5–10 minutes.
   - When it asks a question, press **Enter** for Yes.
   - When it asks to **allow Chitti through the firewall**, say Yes and click **Yes** on the Windows permission pop-up.
3. When it's done, the dashboard opens in your browser with the **setup wizard**.
4. **Keep the black window open** while you use Chitti. Closing it stops her.

> 📷 *screenshot: install window finished*

---

## Step 4: Setup wizard, part 1: keys

Paste your **Claude key** and click **Save**. Then paste your **Sarvam key** if you have one, or skip it. Click **Next**.

> 📷 *screenshot: wizard step 1*

---

## Step 5: Setup wizard, part 2: install Chitti on the board

1. Plug the board into the PC with the USB-C cable. The wizard shows **Board found on COM…** ✓
2. Leave **Erase the board first** ticked the first time.
3. Tick **Save a backup** only if you might want the original Waveshare demo back. It adds about 6 minutes.
4. Click **Install Chitti** and watch the progress bar. **Don't unplug the board** until it says Chitti is installed. Her face appears on the screen.

**If it fails:** the wizard says what to do. Usually:
- try another cable;
- close Arduino IDE or a serial monitor;
- or hold **BOOT**, tap **RESET**, release **BOOT**, then click **Try again**.

> 📷 *screenshot: wizard installing, with the progress bar*

---

## Step 6: Setup wizard, part 3 and 4: Wi-Fi and say hi

1. Keep the board plugged in. Click **Scan** and pick your Wi-Fi, or type its name.
   - It must be **2.4 GHz**. If your router shows two names, use the one without "5G".
   - It must be the **same Wi-Fi your PC uses**, or the same router if your PC uses a LAN cable.
   - Don't use a **guest** Wi-Fi; it stops Chitti from finding your PC.
2. Type the password and click **Send to Chitti**. She restarts and connects.
3. The first time, Windows Firewall may ask about Python. Tick **Private networks** and click **Allow**.
4. When the wizard says **Chitti is online! 🎉**, click **Start talking**, tap her screen and say **"హాయ్ చిట్టి!"**

> 📷 *screenshot: wizard "Chitti is online"*

---

## Step 7: Make her yours (dashboard, then Settings)

- **Personality:** Roaster (friendly roasting), Funny, Chill or Caring.
- **Telugu style:** Telgish, Hyderabadi, Godavari, Gen Z and more.
- **Voice:** Sarvam voices (needs a Sarvam key) or free voices.
- **Words:** words she should use (e.g. రా, రేయ్) and words she must never say.
- **Brain:** Haiku (cheap, fast) or Sonnet (smarter, costs more).
- **City:** for weather questions.

---

## Every day after that

1. Switch Chitti on.
2. Double-click **`Start Chitti.bat`** on your PC, and keep the PC awake (don't close the laptop lid).
3. Talk!

---

## Still stuck?

See **Troubleshooting** in [README.md](README.md#9-troubleshooting). The **Help** section of the dashboard shows what's connected and what isn't.

*Chitti is free and provided as-is. The author is not responsible for any damage, data loss or API charges. See the [disclaimer](README.md#11-disclaimer-please-read).*

---

## 👤 About the Author

Created and maintained by **Chaitanya Eshwar Prasad**. Connect with me or follow my work:

[![Website](https://img.shields.io/badge/Website-chaitanyaeshwarprasad.com-005F9E?style=for-the-badge&logo=google-chrome&logoColor=white)](https://chaitanyaeshwarprasad.com)
[![GitHub](https://img.shields.io/badge/GitHub-chaitanyaeshwarprasad-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/chaitanyaeshwarprasad)
[![LinkedIn](https://img.shields.io/badge/LinkedIn-Chaitanya%20Eshwar%20Prasad-0A66C2?style=for-the-badge&logo=linkedin&logoColor=white)](https://linkedin.com/in/chaitanya-eshwar-prasad)
[![YouTube](https://img.shields.io/badge/YouTube-@chaitanya.eshwar.prasad-FF0000?style=for-the-badge&logo=youtube&logoColor=white)](https://www.youtube.com/@chaitanya.eshwar.prasad)
[![Instagram](https://img.shields.io/badge/Instagram-@acep.tech.in.telugu-E4405F?style=for-the-badge&logo=instagram&logoColor=white)](https://instagram.com/acep.tech.in.telugu)
