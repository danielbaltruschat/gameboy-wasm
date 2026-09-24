# Third-Party Notices

## SameBoy DMG Boot ROM

`include/sameboy_dmg_boot_rom.h` contains the 256-byte, open,
reverse-engineered DMG boot ROM built from SameBoy commit
`213a12ce93d66b105a113debd9396306066a7cfc`.

- Source: <https://github.com/LIJI32/SameBoy/tree/213a12ce93d66b105a113debd9396306066a7cfc/BootROMs>
- Artifact SHA-256: `6f64da4cecd7e54e2f928eb3e3ba7810a7a567d0d247cc71737d1771e073a916`
- Copyright (c) 2015-2026 Lior Halphon

It is a redistributable compatibility boot ROM, not Nintendo's physical DMG-B
mask ROM. The frontend allows a user-provided 256-byte boot-ROM override.

```text
Expat License

Copyright (c) 2015-2026 Lior Halphon

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## PicoSHA2

`picosha2` is used by the web binding to derive the IndexedDB key from the
complete ROM contents.

- Source: <https://github.com/okdshin/PicoSHA2>
- Version: 1.0.1
- License: MIT
