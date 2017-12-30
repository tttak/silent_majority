#!/bin/bash

echo "testing started"
pwd

echo -n "wget apery_sdt5.zip .. "
wget --load-cookies /tmp/cookies.txt \
  `wget --keep-session-cookies --save-cookies=/tmp/cookies.txt \
      'https://drive.google.com/uc?id=1tH8IOvjCKBi9IghZpbwGDcXyj3GDva8P' -q -O - \
          | perl -nle 'if($_=~/download-link.*?href="(.*?)"/i){$str=$1;$str=~s/&amp;/&/g;print "https://drive.google.com$str";}'` \
                -q -O apery_sdt5.zip
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
mv apery_sdt5_eval_twig_format 20161007

echo "loading eval.bin.."
cat 20161007/*.bin > /dev/null 2>&1

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

