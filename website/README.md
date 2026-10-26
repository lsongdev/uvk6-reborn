# UVK6 Reborn website

Browser flashing and device tools, built with Preact + htm + native ESM.
No frontend build, package installation, or application server is needed.
`app.js` imports `https://lsong.org/scripts/react/index.js`; the page uses
`https://lsong.org/css/stylesheet.css` and `https://lsong.org/stylesheets/index.css`.
The shared module currently loads htm/Preact from unpkg.com, so these origins
must be reachable. Radio data and backups stay in the browser.

## Preview

From the repository root:

```sh
python3 -m http.server 8080 --directory website
```

Open <http://localhost:8080> in desktop Chrome or Edge. Web Serial requires
HTTPS or localhost and an explicit serial-port selection. Local preview can
use a local raw `.bin`; the firmware manifest is generated only for deployment.

## Device operations

- Flash K6 SI4732, K6 BK1080, or K1 BK1080 raw images using a K-type UART cable,
  38400 baud, 256-byte pages, five attempts per page, and matched acknowledgments.
  Hold PTT while powering on after selecting the port. K1 USB CDC is application-only.
- Reject packed images, wrong vectors, partition overflow and mismatched bootloaders
  (K6 3.x, K1 7.x) before sending a handshake or writing a page. The K6 chip
  variant cannot be detected by the bootloader; the user must select it correctly.
- Power-cycle after flashing, then connect the running `s0v4` application.
  Read storage information, diagnostics and the RF register whitelist, or reboot.
- Back up calibration (K6 `0x1E00..0x1FFF`; K1 the 4 KiB sector at `0x010000`)
  or the full detected physical storage (512 KiB / 2 MiB). Reads use 128-byte chunks,
  matched addresses and sizes, pacing, and a bounded retry budget. Only completed
  backups are offered for download, with optional SHA-256 metadata.
- A cancelled/failed backup closes the session. Reconnect before another operation.
  No storage write or arbitrary register write commands are exposed. The firmware
  currently has no remote tuning or channel-editing interface.

## GitHub Pages

The `pages` job in `.github/workflows/c-cpp.yml` runs after all host tests and
three firmware builds pass on `main`. `prepare.py` verifies the artifacts'
board, variant, commit, vector table, size and SHA-256, then copies the static
files and matching binaries to the Pages artifact. The deployed site and firmware
always come from the same commit. There is no frontend build step.

Repository Settings → Pages → Source must be **GitHub Actions**. The account's
custom Pages domain may determine the final project URL. All asset and firmware
paths are relative, so both a project path and a custom domain work.

```sh
npm --prefix website test
python3 website/tests/prepare_test.py
```

These tests simulate devices and never open a physical serial port. Browser
flashing still requires hardware confirmation on each board.
