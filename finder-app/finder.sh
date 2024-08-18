#!/bin/sh
# Implement for the assignment 1
# Looking for times of search string appears in the specific directory

# Check number of arguments pass the bash script
if [ $# -eq 2 ]
then
    # The variable assignment must have no space at both side of the equal sign "="
    filesdir=$1;
    searchstr=$2;
    if [ -d $1 ]
    then
        numfiles=`find $filesdir -type f | wc -l`
        nummatchs=`grep -r $searchstr $filesdir/* | wc -l`
        # nummatchs=1
        echo "The number of files are $numfiles and the number of matching lines are $nummatchs"
        exit 0
    else
        echo "First argument is not a directory"
        exit 1
    fi
else
    echo "Number of argument of must be 2. Ex: $0 directory searchstring"
    exit 1
fi

