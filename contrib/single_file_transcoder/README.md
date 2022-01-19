# Single File Basis Universal Transcoder

The script `combine.py` creates an _amalgamated_ C++ source file that can be used with or without `basisu_transcoder.h`. This _isn't_ a header-only file but it does offer a similar level of simplicity when integrating into a project.

Create `basisu_transcoder.cpp` from the transcoder sources using:
```
cd basis_universal/contrib/single_file_transcoder

python3 combine.py -r ../../transcoder -o basisu_transcoder.cpp basisu_transcoder-in.cpp
```
Then add the resulting file to your project (see the [example files](examples)).

If certain features will _never__ be enabled, e.g. `BASISD_SUPPORT_BC7_MODE6_OPAQUE_ONLY`, then `combine.py` can be told to exclude files with the `-x` option:
```
python3 combine.py -r ../../transcoder -x basisu_transcoder_tables_bc7_m6.inc -o basisu_transcoder.cpp basisu_transcoder-in.cpp
```
Excluding the BC7 mode 6 support reduces the generated source by 1.2MB, which is the choice taken in `basisu_transcoder-in.cpp` and used in the examples, with `create_transcoder.sh` running the above script, creating the final `basisu_transcoder.cpp`.

The combiner script can also generate separate amalgamated header and source files, using the `-k` option to keep the specified inline directive, and `-p` to keep the `#pragma once` directives in the header:
```
python3 combine.py -r ../../transcoder -o basisu_transcoder.h -p ../../transcoder/basisu_transcoder.h

python3 combine.py -r ../../transcoder -x basisu_transcoder_tables_bc7_m6.inc -k basisu_transcoder.h -o basisu_transcoder.cpp basisu_transcoder-in.cpp 

```

Note: the amalgamation script was tested on Windows and Mac, requiring Python 3.8, with a fallback implementation as a shell script that will run on pretty much anything.

Why?
----

Because all it now takes to support Basis Universal is the addition of a single file, two if using the header, with no configuration or further build steps (the out-of-the-box defaults tailor the included formats for various platforms). 

The library is small, adding, for example, around 250kB to an Emscripten compiled WebAssembly project (with transcoding disabled for BC7 and ATC; disabling ASTC will remove a further 64kB, and `gzip` will approximately half the `wasm` file).
