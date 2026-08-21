# Third-party notices

The MIT license in [`LICENSE`](LICENSE) covers the original FE8 Extended
Frontend code in this repository. It does not relicense third-party projects,
ROMs, game assets, or trademarks.

## mGBA

The application builds and statically links the pinned
[`third_party/mgba`](third_party/mgba) submodule from
[mGBA](https://github.com/mgba-emu/mgba). mGBA is licensed under the Mozilla
Public License 2.0. Its corresponding source and license are available in that
submodule and from the upstream repository; distributions of the executable
must comply with the MPL-2.0 requirements for the linked mGBA code.

## SDL2 and zlib

The frontend links against SDL2 and zlib supplied by the build environment.
SDL2 uses the zlib license. zlib uses the zlib license. See the corresponding
installed packages or upstream projects for their complete notices.

## mGBA TV Mode and Scanlines shaders

The macOS video presets include adapted copies of mGBA's `TV Mode` and
`Scanlines` fragment shaders:

- TV Mode Shader, Copyright (C) 2022 Dominus Iniquitatis
- Scanlines Shader, Copyright (C) 2017 Dominus Iniquitatis

Both are distributed under the following MIT license:

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the “Software”), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## font8x8 basic Latin bitmap font

The portable text renderer includes Daniel Hepper's `font8x8_basic` data,
based on public-domain IBM VGA font data. The upstream file declares the
font data to be in the public domain.

## Fire Emblem decompilation reference

The optional [`reference/fireemblem8u`](reference/fireemblem8u) submodule points
to the Fire Emblem Universe `fireemblem8u` repository used as a technical
reference for data layouts. It is not compiled, packaged, or covered by this
project's MIT license. Its repository does not provide a general-purpose
software license. No ROM, Nintendo-owned game data, artwork, music, or other
copyrighted game content is distributed here. Users must supply legally
obtained ROM files themselves.
