#!/bin/sh

echo "Amalgamating files... this can take a while"
./combine.sh -r "../../transcoder" -o basisutranslib.cpp basisutranslib-in.cpp
# Did combining work?
if [ $? -ne 0 ]; then
  echo "Combine script: FAILED"
  exit 1
fi
echo "Combine script: PASSED"
