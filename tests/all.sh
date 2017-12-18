#!/bin/bash

echo "testing started"
pwd

echo -n "wget apery_sdt5.zip .. "
wget -q https://github.com/HiraokaTakuya/apery/releases/download/SDT5/apery_sdt5.zip -O apery_sdt5.zip
if [ $? != 0 ]; then
  echo "testing failed(wget)"
  exit 1
fi
echo "done."

unzip -o apery_sdt5.zip
if [ $? != 0 ]; then
  echo "testing failed(unzip)"
  exit 1
fi

cp ../build/visualc14/silent_majority/benchmark.sfen .

echo "loading eval.bin.."
cat apery_sdt5/bin/20171106/*.bin > /dev/null 2>&1

./moves.sh
if [ $? != 0 ]; then
  echo "testing failed(moves.sh)"
  exit 1
fi

./benchmark.sh
if [ $? != 0 ]; then
  echo "testing failed(benchmark.sh)"
  exit 1
fi

exit 0

