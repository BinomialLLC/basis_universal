# Single File Basis Universal Examples

The examples `#include` the generated `basisu_transcoder.cpp` directly but work equally as well when including `basisu_transcoder.h` and compiling the amalgamated source separately.

`emscripten.cpp` is a bare-bones [Emscripten](https://github.com/emscripten-core/emscripten) compiled WebGL demo picking the best transcoder format for the sample texture (see the [original PNG image](testcard.png)).

The example files in this directory are released under a [Creative Commons Zero license](https://creativecommons.org/publicdomain/zero/1.0/).
