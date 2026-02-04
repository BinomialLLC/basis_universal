@ECHO OFF
REM Example: "runw.bat test_images/xmen.png"
REM Example: "runw.bat /bik/bik1.png"

REM wasmtime --wasm threads=yes --wasi threads=yes --dir=. --dir=.. --dir=..\test_files::/test_files --dir=d:/dev/test_images::/test_images --dir=d:/dev/test_images/bik::/bik basisu_mt.wasm %*

wasmtime --wasm threads=yes --wasi threads=yes --dir=. --dir=.. --dir=..\test_files::/test_files basisu_mt.wasm %*
