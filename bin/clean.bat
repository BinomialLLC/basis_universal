@echo off
del *.exr
del *.png
del *.dds
del *.astc
del *.tga

for %%F in (*.ktx) do (
    if /I "%%~xF"==".ktx" (
        echo Deleting "%%F"
        del "%%F"
    )
)
