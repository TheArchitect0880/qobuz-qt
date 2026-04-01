# qobuz-qt

A desktop Qobuz music streaming client built with Qt 6 and Rust.

This was made by reverse engineering the Qobuz Android mobile app to figure out the API, authentication flow, and the qbz-1 audio stream encryption scheme. I didn't know Rust or C++ and needed something working very quickly, so I used AI to write most of the code for me. As a result, the codebase is not something I would recommend using. It works, but it is AI slop.

We only found out about [qbz](https://github.com/vicrodh/qbz) when publishing this to GitHub. If you are looking for a proper Qobuz desktop client, use that instead. It is a much more polished and better implemented project.

Much of the UI structure and layout is based on [spotify-qt](https://github.com/kraxarn/spotify-qt), which provided a great foundation for building a native Qt music player interface.

## Screenshots

<img width="1720" height="1388" alt="image" src="https://github.com/user-attachments/assets/fd7d6e41-cc78-42a1-bcb0-98b354e8ef33" />

<img width="1720" height="1388" alt="image" src="https://github.com/user-attachments/assets/10b2cc77-fc7b-439e-a39b-c805e6250af0" />

<img width="1720" height="1388" alt="image" src="https://github.com/user-attachments/assets/11eb865e-e76c-422a-89ce-75cb6697e6bd" />

## Features

- Login via Qobuz email/password
- Unified search across tracks, albums, and artists
- Album and artist detail pages with metadata, cover art, and biography
- Audio playback with support for Hi-Res 24-bit/192 kHz, Hi-Res 24-bit/96 kHz, CD quality 16-bit lossless, and MP3 320 kbps
- Segmented streaming with qbz-1 AES-128-CTR decryption for Hi-Res and lossless formats
- Plain HTTP streaming for MP3
- Seek support for both segmented and plain streams
- Volume control and ReplayGain normalization
- Gapless playback with next track prefetching
- Autoplay with dynamic track suggestions when the queue ends
- Shuffle mode
- Play queue management: add, remove, reorder, play next
- Library: favorite tracks, albums, and artists
- Playlist management: create, delete, add/remove tracks, follow/unfollow
- Genre browsing with new releases, most streamed, editors picks, and featured playlists
- Last.fm scrobbling with now-playing updates
- MPRIS2 D-Bus integration on Linux for desktop media controls

## Building

### Linux

Debian/Ubuntu:

```
sudo apt install cmake ninja-build qt6-base-dev qt6-svg-dev libssl-dev libasound2-dev libdbus-1-dev pkg-config curl
```

Rust toolchain: install via [rustup](https://rustup.rs).

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows

A prebuilt Windows binary can be downloaded from the [GitHub Actions](https://github.com/TheArchitect0880/qobuz-qt/actions) tab. Select the latest successful run and download the `qobuz-qt-windows-x64` artifact.

To build from source, install Qt 6, Rust, and Visual Studio with CMake, then run:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## License

GPL-3.0 license. See [LICENSE](LICENSE) for details.
