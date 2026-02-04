@ECHO OFF
REM Example: "runw.bat test_images/xmen.png"
REM Example: "runw.bat /bik/bik1.png"

REM wasmtime --dir=. --dir=.. --dir=..\test_files --dir=d:/dev/test_images::/test_images --dir=d:/dev/test_images/bik::/bik basisu_st.wasm %*
wasmtime --dir=. --dir=.. --dir=..\test_files basisu_st.wasm %*
