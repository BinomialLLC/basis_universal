# Single File Basis Universal Transcoder

`basisu_transcoder.cpp` is an amalgamated single-file build of the Basis Universal transcoder, generated with the [single file transcoder](../../single_file_transcoder) `combine.py` script. KTX2 and Zstd support are enabled.

Only the Basis Universal sources are amalgamated. Zstd is intentionally **not** inlined or duplicated: it is left as an ordinary `#include "../zstd/zstd.h"` (via `combine.py`'s `-k` option), and its implementation (`zstd/zstd.c`) is compiled and linked separately by the consuming project. So the single file is just our code.

To regenerate it after changing the transcoder sources:

```sh
cd basis_universal/contrib/single_file_transcoder
python3 combine.py -r ../../transcoder -k ../../zstd/zstd.h \
    -o ../previewers/lib/basisu_transcoder.cpp basisu_transcoder-in.cpp
```

The consuming project must add the `zstd` directory to its include paths and build `zstd/zstd.c` as a separate compilation unit — see [../win/previewers.vcxproj](../win/previewers.vcxproj), which adds `..\..\..\zstd` to the include paths and compiles `..\..\..\zstd\zstd.c`. (The Explorer previewer itself only registers `.basis` files, but the transcoder is built with KTX2/Zstd enabled so it compiles cleanly out of the box.)
