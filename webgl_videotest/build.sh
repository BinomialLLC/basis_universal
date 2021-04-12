# rg - I haven't tested this shell script yet (I use build.bat on Windows)
emcc -s EXPORTED_FUNCTIONS="['allocate', '_malloc', '_free', '_basis_init','_basis_open','_basis_close','_basis_get_has_alpha','_basis_get_num_images','_basis_get_num_levels','_basis_get_image_width','_basis_get_image_height','_basis_get_image_transcoded_size_in_bytes','_basis_transcode_image','_basis_start_transcoding']" -s TOTAL_MEMORY=749993984 -O2 -s ASSERTIONS=0 -I ../transcoder -o basis.js ../transcoder/basisu_transcoder.cpp basis_wrappers.cpp -D BASISD_SUPPORT_KTX2=0
# chmod -R a+rX .
