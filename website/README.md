# UVK6 Reborn website

Browser flashing and device tools, built with Preact + htm + native ESM.
No frontend build, package installation, or application server is needed.
`app.js` imports `https://lsong.org/scripts/react/index.js`; the page uses
`https://lsong.org/css/stylesheet.css` and `https://lsong.org/stylesheets/index.css`.
The shared module serves pinned htm/Preact runtime copies from lsong.org;
the page does not need unpkg.com. Radio data and backups stay in the browser.

## Preview

From the repository root:

```sh
python3 -m http.server 8080 --directory website
```

Open <http://localhost:8080> in desktop Chrome or Edge. Web Serial requires
HTTPS or localhost and an explicit serial-port selection. Local preview can
use a local raw `.bin`; the firmware manifest is generated only for deployment.

## Device operations

Regenerate the website's single-screen firmware preview from the real drawing code:

```sh
python3 tools/ui-preview.py --frames manual-frequency --output website/assets/main-screen.png
```

- Flash K6 SI4732, K6 BK1080, or K1 BK1080 raw images using a K-type UART cable,
  38400 baud, 256-byte pages, five attempts per page, and matched acknowledgments.
  Hold PTT while powering on after selecting the port. K1 USB CDC is application-only.
- Reject packed images, wrong vectors, partition overflow and mismatched bootloaders
  (K6 3.x, K1 7.x) before sending a handshake or writing a page. The K6 chip
  variant cannot be detected by the bootloader; the user must select it correctly.
- Power-cycle after flashing, then connect the running `reborn` application.
  Read storage information, diagnostics and the RF register whitelist, or reboot.
- Back up calibration (K6 `0x1E00..0x1FFF`; K1 the 4 KiB sector at `0x010000`)
  or the full detected physical storage (512 KiB / 2 MiB). Reads use 128-byte chunks,
  matched addresses and sizes, pacing, and a bounded retry budget. Only completed
  backups are offered for download, with optional SHA-256 metadata.
- A cancelled/failed backup closes the session. Reconnect before another operation.
  No raw storage or arbitrary register writes are exposed.

## Channel management

Connect the running firmware, then read all 200 channels before editing. The
website uses `0x0580` / `0x0582` with the canonical 41-byte record documented in
[channel-format.md](../docs/channel-format.md); it never writes raw storage addresses.
Names support ASCII and GB2312 (16 bytes). The tone catalogue matches the firmware.
Save edits to the pending list, preview all changed fields and deletions, then
explicitly confirm writing. JSON exports and imports use `reborn-channels-v1`;
only channel numbers present in an imported file are changed, including explicit
null entries for deletions. Invalid or duplicate records reject the entire import.

Before writing, the browser downloads an original-channel JSON backup. Each write
first checks the device channel against the original read, waits for the firmware's
status and reads the channel again. A conflict, refusal, timeout or mismatch stops
the batch. Writes are never automatically retried; completed channels stay saved.
After an uncertain result, export pending edits and reread before continuing.
Disconnected drafts remain exportable but cannot be written until rereading.
The browser has no hardware access until the user selects a port and confirms actions.

The GB2312 lookup in `channel-data.js` is generated from Python's standard
`gb2312` codec over A1–F7 / A1–FE; CTCSS and DCS values come from `src/core/dcs.c`.
The host tests check the tone catalogue against that source.

## GitHub Pages

The `pages` job in `.github/workflows/c-cpp.yml` runs after all host tests and
three firmware builds pass on `main`. `prepare.py` verifies the artifacts'
board, variant, commit, vector table, size and SHA-256, then copies the static
files and matching binaries to the Pages artifact. The deployed site and firmware
always come from the same commit. There is no frontend build step.
Published local scripts, module imports, styles and images use `?v=<commit>`
URLs so browser caches cannot mix files from different deployments. Firmware
downloads use the manifest's commit as their version and still verify SHA-256.
An import map applies the same deployment version to the shared Preact/htm
runtime URLs, including transitive imports, to bypass cached missing-file responses
and keep hooks and rendering on the same Preact module instance.

Repository Settings → Pages → Source must be **GitHub Actions**. The account's
custom Pages domain may determine the final project URL. All asset and firmware
paths are relative, so both a project path and a custom domain work.

```sh
npm --prefix website test
python3 website/tests/prepare_test.py
```

These tests simulate devices and never open a physical serial port. Browser
flashing still requires hardware confirmation on each board.
