#!/bin/sh

# Temporary compiled binary
OUT_FILE="tempbin"

echo "Amalgamating files... this can take a while"
# Use the faster Python script if we have 3.8 or higher
if python3 -c 'import sys; assert sys.version_info >= (3,8)' 2>/dev/null; then
  ./combine.py -r ../../transcoder -o basisu_transcoder.cpp basisu_transcoder-in.cpp
else
  ./combine.sh -r ../../transcoder -o basisu_transcoder.cpp basisu_transcoder-in.cpp
fi
# Did combining work?
if [ $? -ne 0 ]; then
  echo "Combine script: FAILED"
  exit 1
fi
echo "Combine script: PASSED"

# Compile the generated output
which cc > /dev/null
if [ $? -ne 0 ]; then
  echo "(Skipping compile test)"
else
  cc -lm -std=c++11 -lstdc++ -Wall -Wextra -Werror -Os -g0 -fno-exceptions -fno-rtti -o $OUT_FILE examples/simple.cpp
  # Did compilation work?
  if [ $? -ne 0 ]; then
    echo "Compiling simple.cpp: FAILED"
    exit 1
  fi
  # Run then delete the compiled output
  ./$OUT_FILE
  retVal=$?
  rm -f $OUT_FILE
  # Did the test work?
  if [ $retVal -ne 0 ]; then
    echo "Running simple.cpp: FAILED"
    exit 1
  fi
  echo "Running simple.cpp: PASSED"
fi
