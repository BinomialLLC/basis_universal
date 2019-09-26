# Single File Basis Universal Transcoder

The script `combine.sh` creates an _amalgamated_ C++ source file that can be used with or without `basisu_transcoder.h`. This _isn't_ a header-only file but it does offer a similar level of simplicity when integrating into a project.

Create `basisutranslib.cpp` from the transcoder source using:
```
cd basis_universal/contrib/single_file_transcoder
./combine.sh -r ../../transcoder -o basisutranslib.cpp basisutranslib-in.cpp
```
Then add the resulting file to your project (see the [example files](examples)).

`create_translib.sh` will run the above script, creating the file `basisutranslib.cpp`.

Why?
----

Because all it now takes to support Basis Universal is the addition of a single file, two if using the header, with no configuration or further build steps (the out-of-the-box defaults tailor the included formats for various platforms). 

The library is small, adding, for example, 249kB to an Emscripten compiled WebAssembly project (with transcoding disabled for BC7 and ATC; disabling ASTC can remove a further 64kB, and `gzip` will approximately half the `wasm` file).
