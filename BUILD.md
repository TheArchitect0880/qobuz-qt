# Building qobuz-qt

## Dependencies

### Debian / Ubuntu
```bash
sudo apt install -y \
    cmake \
    ninja-build \
    qt6-base-dev \
    qt6-svg-dev \
    libssl-dev \
    libasound2-dev \
    libdbus-1-dev \
    pkg-config \
    curl

# Rust toolchain
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
```

### Alpine Linux
```bash
sudo apk add \
    cmake \
    ninja \
    qt6-qtbase-dev \
    qt6-qtsvg-dev \
    openssl-dev \
    alsa-lib-dev \
    dbus-dev \
    pkgconf \
    curl \
    musl-dev \
    gcc \
    g++

# Rust toolchain
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
```

> **Note:** CPAL uses ALSA on Alpine. If you prefer PipeWire/PulseAudio,
> install `pipewire-alsa` so ALSA routes through it automatically.

### Arch Linux
```bash
sudo pacman -S cmake ninja qt6-base qt6-svg openssl alsa-lib rust
```

### Fedora
```bash
sudo dnf install cmake ninja-build qt6-qtbase-devel qt6-qtsvg-devel \
    openssl-devel alsa-lib-devel rust cargo
```

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The first build will take longer as it compiles the Rust backend via
[Corrosion](https://github.com/corrosion-rs/corrosion).

## Run

```bash
./build/qobuz-qt
```

On first launch a login dialog appears.  Enter your Qobuz email and password.
Credentials are stored via `QSettings` in the standard user config directory.

## Audio Quality

Open **File → Settings** to choose the preferred streaming quality:
- Hi-Res 24-bit/192 kHz (format 27)
- Hi-Res 24-bit/96 kHz  (format 7)
- CD 16-bit              (format 6)  ← default
- MP3 320 kbps           (format 5)

## Architecture

```
qobuz-qt/
├── CMakeLists.txt          Root build file (uses Corrosion for Rust)
├── Cargo.toml              Cargo workspace
├── rust/                   Rust backend (static library)
│   ├── Cargo.toml
│   ├── include/
│   │   └── qobuz_backend.h C header for FFI
│   └── src/
│       ├── lib.rs          extern "C" API + Tokio runtime
│       ├── api/            Qobuz HTTP API client (reqwest + tokio)
│       └── player/         Audio playback (Symphonia decoder + CPAL output)
└── src/                    C++ / Qt 6 frontend
    ├── backend/            Qt QObject wrapper around the Rust library
    ├── view/               Toolbar, main content, side panel
    ├── list/               Track list, library sidebar
    ├── model/              TrackListModel (QAbstractTableModel)
    ├── dialog/             Login, Settings dialogs
    ├── widget/             VolumeButton, ClickableSlider
    └── util/               AppSettings (QSettings), Icon helpers
```
