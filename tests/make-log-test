#!/usr/bin/env bash

line=
count=0
total=50000
echo "#include <stdio.h>"
echo "#include <qb/qblog.h>"
echo "extern void log_dict_words(void);"
echo "void log_dict_words(void) {"

while read  w
do
	if [ $count -eq 0 ]
	then
		line="    qb_log(LOG_DEBUG, \"%d : %s %s %s\", $total"
	fi
	line="$line, \"$w\""
	let count="$count+1"
	if [ $count -eq 3 ]
	then
		line="$line );"
		count=0
		let total="$total-1"
		echo $line
		if [ $total -eq 0 ]
		then
			echo "}"
			exit 0
		fi
	fi
done < /usr/share/dict/words

echo "}"

