# Soon ✦

A little countdown gadget for a LilyGO T-Display. She plugs it in, connects it
to her WiFi from her phone, and it counts down to the big day — and whenever
you push a new version to GitHub, her device quietly updates itself.

```
you: edit code → git tag v0.2.0 → git push
                     │
                     ▼
        GitHub Actions builds the firmware
        and publishes it as a Release
                     │
                     ▼
her device checks the latest release every 6 hours
        → sees a newer version → installs it
        → shows a progress bar → reboots updated
```

## Which board do I have?

Flip the board over — the model name is printed on the back.

| On the back | Screen | Build target / firmware file |
|---|---|---|
| `T-Display` | 1.14" LCD (small, ~2.5 cm) | `tdisplay` → `soon-tdisplay.bin` |
| `T-Display-S3` | 1.9" LCD (big, most of the board) | `tdisplay-s3` → `soon-tdisplay-s3.bin` |
| `T-Display-S3 AMOLED` | 1.91" AMOLED (wide, very vivid) | `tdisplay-s3-amoled` → `soon-tdisplay-s3-amoled.bin` |

**Ben's board is the AMOLED** — use `tdisplay-s3-amoled`. If the back says
something else entirely (T-QT, a tiny 0.42" screen…), don't flash anything —
the pin maps won't match; ask Claude to add that board.

## One-time setup (about 15 minutes)

### 1. Create the GitHub repo

1. Sign up / sign in at [github.com](https://github.com).
2. Create a new repository named **`soon`**. It must be **Public** —
   the device downloads updates without logging in. (Your WiFi password is
   never in this code; it lives only on the device itself.)

### 2. Point the firmware at your repo

Open `src/config.h` and change one line:

```cpp
#define GITHUB_OWNER "YOUR_GITHUB_USERNAME"   // <-- put your username here
```

### 3. Push the code and ship v0.1.0

Install [Git for Windows](https://git-scm.com/download/win) if you don't have
it, then in a terminal:

```bash
cd %USERPROFILE%\Documents\soon
git init -b main
git add .
git commit -m "Soon v0.1: wifi setup, OTA updates, countdown"
git remote add origin https://github.com/YOUR_GITHUB_USERNAME/soon.git
git push -u origin main

git tag v0.1.0
git push origin v0.1.0        # <-- this triggers the build
```

Watch the **Actions** tab on GitHub — after ~3 minutes a release **v0.1.0**
appears under **Releases** with the firmware files attached.

### 4. First flash (USB — one time only)

After this one, all updates happen over WiFi.

**Option A — browser flasher (no software installs):**

1. Download `soon-<your-board>-full.bin` from your GitHub release.
2. Plug the board into your PC with USB.
   *T-Display-S3 only:* if no serial port shows up, hold the **BOOT** button
   while plugging in to enter flashing mode.
3. In Chrome or Edge, open <https://espressif.github.io/esptool-js/>,
   click **Connect**, and pick the board's serial port.
   *(Classic T-Display may need the
   [CH9102 driver](https://www.wch-ic.com/downloads/CH343SER_ZIP.html) on
   Windows if no port appears.)*
4. Set **Flash Address** to `0x0`, choose the `-full.bin` file, hit
   **Program**, and wait for it to finish. Unplug/replug the board.

**Option B — PlatformIO (nicer if you'll be developing):**

1. Install [VS Code](https://code.visualstudio.com) and its **PlatformIO IDE**
   extension.
2. Open this folder, pick the environment for your board
   (`tdisplay` or `tdisplay-s3`) in the status bar, and click **Upload** (→).

## What she does at home (the fun part)

1. Plug it in. The screen says: **join the WiFi network `Soon-Setup`** from
   your phone.
2. A setup page pops up automatically (or she browses to `192.168.4.1`).
3. She taps her home network, types the password, hits **Save**.
4. Screen shows the countdown. Done — no app, no account.

You can pre-set the countdown title/date on that same page, or edit it later
from any device on her network at **http://soon.local**.

## Shipping an update (the whole point!)

```bash
# ...edit the code, then:
git commit -am "Snow falls on the countdown screen in December"
git tag v0.2.0
git push origin main v0.2.0
```

That's it. GitHub Actions builds and publishes the release, and her device
picks it up within 6 hours (or instantly if she presses the button). Version
numbers must go up (`v0.1.0` → `v0.2.0`) — the device only installs versions
newer than what it's running.

To test on your own board before tagging, flash over USB with PlatformIO
(`Upload`), which builds a local `0.0.0-dev` version — release builds will
always "win" over it.

## Pages & buttons

Three pages, cycled by swiping the screen (touch models) or short-pressing
the side button: **countdown** (big mint pixel number) → **her message**
(set it on the settings page, or edit `message.txt` in this repo — the
device pulls it every 30 min whenever it has WiFi) → **WiFi / device info**
(setup instructions when offline, plus a build tag for debugging).

| Button | Press | Does |
|---|---|---|
| Side (not BOOT) | short | next page |
| Side (not BOOT) | hold 6 s | forget WiFi + restart |
| BOOT | short | screen on / off |

## Project layout

```
platformio.ini            board definitions + pin maps (both T-Display flavors)
src/main.cpp              state machine: setup → connect → countdown
src/config.h              ← the only file you must edit (GitHub username)
src/portal.cpp/.h         captive WiFi portal + settings page (soon.local)
src/portal_html.h         the setup web page (HTML/CSS/JS)
src/updater.cpp/.h        checks GitHub releases, flashes updates, MD5-verified
src/ui.cpp/.h             all screens: countdown, confetti, progress bar...
src/settings.cpp/.h       saved WiFi + countdown config (survives reboots)
scripts/version.py        stamps the git tag into the firmware as FW_VERSION
.github/workflows/        the build-and-release pipeline
```

## Troubleshooting

- **Actions tab shows a red X** — open the failed step's log; a typo in
  `config.h` or a compile error will show there. Nothing ships unless the
  build succeeds, so a bad tag can't brick her device.
- **Device says it can't reach the WiFi** after moving/router change — hold
  the top button 6 seconds to re-run setup, or she'll get setup mode
  automatically after a few failed attempts.
- **`soon.local` doesn't load** — some routers block mDNS; find the device's
  IP in the router's client list instead.
- **Update seems stuck** — power-cycling is always safe; updates are written
  to a spare flash slot and only take effect once fully verified.

## Security notes (honest fine print)

Update downloads run over HTTPS but skip certificate validation
(`setInsecure()` in `updater.cpp`), which keeps the code simple and immune to
CA-rotation breakage; binaries are MD5-checked against the release manifest.
For a countdown gift this is a reasonable trade-off — someone would need to be
inside her network actively intercepting traffic to matter. If you ever want
proper TLS pinning or signed firmware, that's a contained upgrade in
`updater.cpp`.

## Ideas for her next updates

Snow in December · a birthday-morning message from you · rotating photos ·
weather for her city · a second countdown · "days since" mode for
anniversaries. Each one is just a `git tag` away — that's why you built it
this way.
