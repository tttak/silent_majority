#!/bin/bash

error()
{
  echo "benchmark testing failed on line $1"
  exit 1
}
trap 'error ${LINENO}' ERR

cp ../build/visualc14/silent_majority/benchmark.sfen .

echo "benchmark testing started"

./apery-by-clang bench | tee result.txt

# resignを除くbestmoveの応答が5つ無い場合は失敗
rtn=`grep -v resign result.txt | grep bestmove | wc -l`
if [ "x${rtn}" != "x5" ]; then
  echo "benchmark testing failed(bestmove?)"
  exit 1
fi

rm result.txt
echo "---"
echo "benchmark testing OK"

