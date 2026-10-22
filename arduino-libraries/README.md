# Arduino libraries

The Arduino demos in this repository build with the libraries in this folder, so a demo and the library it was tested with stay together.

| Folder | Library | Pinned at | Pin |
|---|---|---|---|
| [`Warblet/`](Warblet/README.md) | The Warblet Arduino library, from [Robota-LLC/Warblet-Arduino](https://github.com/Robota-LLC/Warblet-Arduino) | commit `feb30fb0eb7d654e64eed6ac04d861bec144737b` | [`Warblet.pin.json`](Warblet.pin.json) |

`Warblet/` is a copy of the library's repository at that commit, byte for byte: its `library.properties`, `LICENSE`, `README.md`, `docs/`, `src/` and `examples/`, and nothing else. `Warblet.pin.json` names the commit and lists each file's SHA-256, and the catalog's checks refuse a copy that differs from it, or from the library's repository at that commit. The library has no release yet; its first release, v0.1.0, replaces this pin.

Do not edit the files here. Change the library in its own repository, then copy the new commit here and write the pin again.

## Use it in the Arduino IDE

Copy `Warblet/` into the `libraries` folder of your Arduino sketchbook (`Documents/Arduino/libraries` on Windows and macOS, `~/Arduino/libraries` on Linux), then restart the IDE. With arduino-cli, pass the folder instead: `arduino-cli compile --library arduino-libraries/Warblet ...`.

The library is MIT licensed; see [`Warblet/LICENSE`](Warblet/LICENSE).
