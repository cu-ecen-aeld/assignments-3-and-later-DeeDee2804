#!/bin/bash
# Implement for the assignment 1
# Write the requested string in file name provided or overwrite if existed

# Check number of arguments pass the bash script
if [ $# -eq 2 ]
then
    # The variable assignment must have no space at both side of the equal sign "="
    writefile=$1;
    writestr=$2;

    # Check if the input file path is full path of linux system
    if [[ $writefile != /* ]]
    then
        echo "$writefile is not a full path"
        exit 1
    fi

    # Check if the input file existed or not
    if [ -f $1 ]
    then
        echo "$writestr" > $writefile
        echo "File $writefile is overwrite with content $writestr"
    elif [ -d $1 ]
    then
        echo "$writefile is a directory"
        exit 1
    else
        touch $writefile
        echo $writestr > $writefile
        echo "File $writefile is created with content $writestr"
    fi
else
    echo "Number of argument of must be 2. Ex: $0 writefile writestring"
    exit 1
fi