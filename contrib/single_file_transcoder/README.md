# Single File Basis Universal Transcoder

The script `combine.sh` creates an _amalgamated_ C++ source file that can be used with or without `basisu_transcoder.h`. This _isn't_ a header-only file but it does offer a similar level of simplicity when integrating into a project.

Create `basisutranslib.cpp` from the transcoder source using:
```
cd basis_universal/contrib/single_file_transcoder
./combine.sh -r ../../transcoder -o basisutranslib.cpp basisutranslib-in.cpp
```
Then add the resulting file to your project (see the [example files](examples)).

If certain features will _never__ be enabled, e.g. `BASISD_SUPPORT_BC7_MODE6_OPAQUE_ONLY`, then `combine.sh` can be told to exclude files completely:
```
./combine.sh -r ../../transcoder -x basisu_transcoder_tables_bc7_m6.inc -o basisutranslib.cpp basisutranslib-in.cpp
```
Excluding the BC7 mode 6 support reduces the generated source by 1.2MB, which is the choice taken in both `basisutranslib-in.cpp` and `create_translib.sh`, will run the above script, creating the final `basisutranslib.cpp`.

Note: the amalgamation script will run on pretty much anything but is _extremely_ slow on Windows with the `bash` included with Git.

Why?
----

Because all it now takes to support Basis Universal is the addition of a single file, two if using the header, with no configuration or further build steps (the out-of-the-box defaults tailor the included formats for various platforms). 

The library is small, adding, for example, around 250kB to an Emscripten compiled WebAssembly project (with transcoding disabled for BC7 and ATC; disabling ASTC will remove a further 64kB, and `gzip` will approximately half the `wasm` file).
