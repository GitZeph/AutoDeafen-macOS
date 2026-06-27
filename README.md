# AutoDeafen (macOS)

Automatically deafens **Discord** while you are in gameplay in Geometry Dash,
and undeafens it when you pause, die (optional), or leave the level.

Instead of simulating the Discord "Toggle Deafen" hotkey (macOS Discord ignores
synthetic keystrokes for its global keybinds), this mod talks **directly to the
Discord client over its local IPC socket** and sends `SET_VOICE_SETTINGS`.
No keybind required.

- **Geode:** 5.7.1
- **Geometry Dash:** 2.2081
- **Platform:** macOS (Intel + Apple Silicon)

## Why you need your own Discord application

Discord's RPC scopes (`rpc`, `rpc.voice.write`) are whitelist-only **except for
the owner of the application**. So everyone has to create their own (free)
Discord app and authorize it with their own account. This is a one-time setup.

### Setup steps

1. Go to <https://discord.com/developers/applications> and click
   **New Application**. Give it any name.
2. In **OAuth2**, copy the **Client ID** and **Client Secret**.
3. Still in **OAuth2 → Redirects**, add this redirect and save:
   ```
   http://localhost:8000
   ```
4. In Geometry Dash, open the mod's settings and paste the **Client ID** and
   **Client Secret**.
5. Make sure **Discord is running**, then open a level, pause, and click the
   gear button (top-left). Press **Authorize**. Your browser opens Discord's
   consent screen — accept it, then return to the game.

After that the OAuth token is stored locally and the mod reconnects to Discord
automatically on each launch (refreshing the token when needed).

## Gameplay behaviour

| Event                              | Action                          |
| ---------------------------------- | ------------------------------- |
| Reached the configured percentage  | **Deafen**                      |
| Paused                             | **Undeafen**                    |
| Resumed from pause                 | **Deafen** (if it was deafened) |
| Died / Game Over                   | **Undeafen** (optional)         |
| Reset / retry                      | **Undeafen**                    |
| Left the level                     | **Undeafen**                    |

## Settings

| Setting               | Type   | Default | Description                                                        |
| --------------------- | ------ | ------- | ------------------------------------------------------------------ |
| `Enabled`             | bool   | `true`  | Master switch for the auto-deafen behaviour.                       |
| `Minimum Percent`     | int    | `0`     | Only deafen once you reach this percentage of the level (0-100).   |
| `Undeafen On Death`   | bool   | `false` | Undeafen when you die instead of staying deafened until you exit.  |
| `Discord Client ID`   | string | `""`    | Client ID of your Discord application.                             |
| `Discord Client Secret` | string | `""`  | Client Secret of your Discord application (used once for OAuth).   |

## Building

Requires the Geode SDK (`GEODE_SDK` env var) and the Geode CLI.

```bash
geode build
```

## Project structure

```
.
├── CMakeLists.txt        # macOS-only build (pure C++/POSIX)
├── mod.json              # metadata + settings
├── README.md
└── src/
    ├── DiscordIPC.hpp/.cpp   # Discord IPC client over the local Unix socket
    ├── OAuthServer.hpp/.cpp  # tiny localhost:8000 server to catch the OAuth redirect
    └── main.cpp              # Geode hooks, OAuth flow, wiring
```

## Troubleshooting

- **"Impossibile connettersi alla socket IPC di Discord"** — Discord isn't
  running, or it's a sandboxed build that puts its socket elsewhere. Start the
  normal Discord desktop app before authorizing.
- **"AUTHENTICATE fallito"** — the stored token expired or the scopes are wrong.
  Re-run the Authorize step from the pause-menu gear button.
- **"porta 8000 occupata"** — something else is using port 8000; close it and
  try again.
