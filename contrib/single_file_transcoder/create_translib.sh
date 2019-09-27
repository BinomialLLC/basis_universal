#!/bin/sh

echo "Amalgamating files... this can take a while"
echo "Note: basisu_transcoder_tables_bc7_m6.inc is excluded"
./combine.sh -r "../../transcoder" -x basisu_transcoder_tables_bc7_m6.inc -o basisutranslib.cpp basisutranslib-in.cpp
# Did combining work?
if [ $? -ne 0 ]; then
  echo "Combine script: FAILED"
  exit 1
fi
echo "Combine script: PASSED"
