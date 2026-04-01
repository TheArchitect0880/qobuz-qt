# qobuz-qt

A desktop Qobuz music streaming client built with Qt 6 and Rust.

This was made by reverse engineering the Qobuz Android mobile app to figure out the API, authentication flow, and the qbz-1 audio stream encryption scheme. I didn't know Rust or C++ and needed something working very quickly, so I used AI to write most of the code for me. As a result, the codebase is not something I would recommend using. It works, but it is AI slop.

We only found out about [qbz](https://github.com/vicrodh/qbz) when publishing this to GitHub. If you are looking for a proper Qobuz desktop client, use that instead. It is a much more polished and better implemented project.

Much of the UI structure and layout is based on [spotify-qt](https://github.com/kraxarn/spotify-qt), which provided a great foundation for building a native Qt music player interface.

## Screenshots

<!-- Add screenshots here -->

## Features

- Login via Qobuz email/password (OAuth2)
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

### Dependencies

Debian/Ubuntu:

```
sudo apt install cmake ninja-build qt6-base-dev qt6-svg-dev libssl-dev libasound2-dev libdbus-1-dev pkg-config curl
```

Rust toolchain: install via [rustup](https://rustup.rs).

### Build

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## License

GPL-3.0 license. See [LICENSE](LICENSE) for details.
