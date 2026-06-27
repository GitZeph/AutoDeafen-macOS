# AutoDeafen (macOS)

**The first working auto-deafen for Discord on macOS.** 🎉

Stop blasting your friends' ears every time you fail a hard part. AutoDeafen (macOS)
automatically **deafens Discord** the moment you start grinding and **undeafens**
it the second you pause, die, or leave the level.

Every other auto-deafen relies on simulating the Discord hotkey, which simply
**does not work on macOS** (Discord ignores synthetic keystrokes). This mod talks
**directly to the Discord client over its local IPC socket**, so it actually
works. No keybind needed.

---

## ✨ Features

- 🔇 Auto-deafens Discord at a configurable level percentage
- 🔊 Auto-undeafens on pause, death (optional), reset, and quit
- 🎯 Talks to Discord natively over IPC — **no keybind, no fake keystrokes**
- 🍎 Built for macOS (Intel + Apple Silicon)

---

## 🛠️ Setup (one time, ~2 minutes)

Discord locks its voice controls behind RPC scopes that are whitelist-only
**unless you own the app**. So you create your own free Discord app once.

### 1. Create a Discord application
- Open the **[Discord Developer Portal](https://discord.com/developers/applications)**
- Click **New Application**, give it any name, and create it

### 2. Grab your credentials
- Go to the **OAuth2** tab
- Copy your **Client ID**
- Click **Reset Secret** (if needed) and copy your **Client Secret**

### 3. Add the redirect URL
- Still in **OAuth2 → Redirects**, click **Add Redirect** and paste:

> `http://localhost:8000`

- Click **Save Changes** ✅

### 4. Paste them into the mod
- In Geometry Dash, open **AutoDeafen's settings**
- Paste your **Client ID** and **Client Secret**

### 5. Authorize
- Make sure **Discord is running**
- Enter any level, **pause**, and click the **🔇 button** in the pause menu
- Press **Authorize** — your browser opens Discord's consent screen
- Accept it, then jump back into the game

That's it. The token is saved locally and the mod reconnects automatically every
launch (and refreshes itself when it expires). You only ever do this once.

---

## ⚙️ Settings

| Setting | Description |
| --- | --- |
| **Enabled** | Master switch for auto-deafen. |
| **Minimum Percent** | Only deafen once you reach this % of the level. |
| **Undeafen On Death** | Undeafen when you die instead of staying deafened until you exit. |
| **Discord Client ID** | Client ID of your Discord application. |
| **Discord Client Secret** | Client Secret of your app (used once for OAuth, stored locally). |

---

## ❓ Troubleshooting

- **"Impossibile connettersi alla socket IPC di Discord"** — Discord isn't
  running. Launch the normal Discord desktop app, then Authorize again.
- **"AUTHENTICATE fallito"** — your token expired or the scopes are wrong.
  Re-run Authorize from the pause-menu button.
- **"porta 8000 occupata"** — another program is using port 8000. Close it and
  retry. Make sure the redirect in your Discord app is exactly
  `http://localhost:8000`.

---

## 💖 Donations

I built this in my spare time and made the first auto-deafen that actually works
on macOS. If it saved your eardrums and you'd like to say thanks, you can tip me
here — totally optional, always appreciated:

### 👉 **[revolut.me/tomasj4ly](https://revolut.me/tomasj4ly)**

Thank you, and happy grinding! 🎮
