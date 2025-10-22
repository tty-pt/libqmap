#!/bin/sh -e

qmap=./bin/qmap

assert() {
	file=snap/$1.txt
	shift
	echo $@ >&2
	if "$@" | diff $file -; then
		return 0;
	else
		echo Test FAILED! $file != $@ >&2
		return 1
	fi
}

rm *.db 2>/dev/null || true

./bin/test | diff expects.txt -

adb=a.db:a:u
bdb=b.db:a
sdb=s.db
cdb=c.db:2u:u
assert 0 $qmap -p 0:1 $adb
assert 01v $qmap -p hi -p hello $bdb
assert 01 $qmap -l $adb
assert 1hello0hi $qmap -l $bdb

assert none $qmap -g hallo $bdb
assert 0 $qmap -g hi $bdb
assert none $qmap -g 2 $bdb
assert none $qmap -g 0 $bdb

assert justhi $qmap -rg hallo $bdb
assert justhi $qmap -rg hi $bdb
assert none $qmap -rg 2 $bdb
assert justhi $qmap -rg 0 $bdb

assert 0 $qmap -q $bdb -g hi $bdb # 0 key
assert 0 $qmap -q $bdb -g hallo $bdb # 0 key
assert none $qmap -q $bdb -g 2 $bdb
assert 0 $qmap -q $bdb -g 0 $bdb

assert justhi $qmap -q $bdb -rg hi $bdb
assert none $qmap -q $bdb -rg hallo $bdb
assert none $qmap -q $bdb -rg 2 $bdb
assert none $qmap -q $bdb -rg 0 $bdb

assert 0 $qmap -q $bdb -q $bdb -g hi $bdb
assert none $qmap -q $bdb -q $bdb -g hallo $bdb
assert none $qmap -q $bdb -q $bdb -g 2 $bdb
assert none $qmap -q $bdb -q $bdb -g 0 $bdb

assert justhi $qmap -q $bdb -q $bdb -rg hi $bdb
assert justhi $qmap -q $bdb -q $bdb -rg hallo $bdb
assert none $qmap -q $bdb -q $bdb -rg 2 $bdb
assert justhi $qmap -q $bdb -q $bdb -rg 0 $bdb

assert hikey $qmap -p 'hi:Hi how are you' $sdb
assert hi $qmap -l $sdb
assert hi2key $qmap -p 'hi2:Hi how are you 2' $sdb
assert empty $qmap -rd 'hi2' $sdb
assert hi $qmap -l $sdb
assert hi2key $qmap -p 'hi2:Hi how are you 2' $sdb
assert empty $qmap -d 'Hi how are you 2' $sdb
assert hi $qmap -l $sdb

# FIXME No DUP PRIMARY support yet
assert 50 $qmap -p 5:9 -p 0:1 $cdb # but this still works
# assert missing $qmap -q $adb -L $cdb
# assert empty $qmap -q $cdb -L $adb
# assert 01 $qmap -q $cdb -rL $adb
assert 5 $qmap -p 5:9 $adb
assert empty $qmap -d 9 $adb
assert 01 $qmap -l $adb
assert 5 $qmap -p 5:9 $adb
assert empty $qmap -d 5:9 $adb
assert 01 $qmap -l $adb
assert 5 $qmap -p 5:9 $adb
assert empty $qmap -rd 9:5 $adb
assert 01 $qmap -l $adb
assert 55 $qmap -p 5:9 -p 5:8 $adb
assert empty $qmap -rd 5 $adb
assert 01 $qmap -l $adb

assert assoc $qmap -a $sdb -rl $bdb
assert assoc-bail $qmap -a $bdb -l $adb
assert assoc3 $qmap -a $bdb -a $sdb -l $adb
assert assoc-bail $qmap -xa $bdb -a $sdb -l $adb
assert assoc-bail2 $qmap -xa $bdb -a $sdb -rl $adb

# TODO test random (normal and reverse?)
# TODO test AINDEX col insert id
