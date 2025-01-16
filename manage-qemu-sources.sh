#!/bin/bash

usage_function () {
    echo "Usage: $0 qemu_directory [diff|sync]"
    exit 0
}

if [ $# -ne 2 ]; then
    usage_function
fi

qemudir=${1}
cmd=${2}

if [ ${cmd} != "diff" -a ${cmd} != "sync" ]; then
    echo "Wring command: choose 'diff' or 'sync'"
    usage_function
fi

diff_function () {
    if [ ! -f ${qemudir}/${src_prefix}/${1} ]; then
        echo "${qemudir}/${src_prefix}/${1} doesn't exist."
        exit 0
    fi
    if [ ! -f ${dst_prefix}/${1} ]; then
        echo "${dst_prefix}/${1} doesn't exist."
        exit 0
    fi
    diff -q ${qemudir}/${src_prefix}/${1} ${dst_prefix}/${1}
    if [ $? -ne 0 ]; then
        diff -Npur ${qemudir}/${src_prefix}/${1} ${dst_prefix}/${1}
    fi
}

sync_warning () {
    echo "This command will ease all the modifications you did"
    echo "in all the qemu files you imported."
    echo -n "Are you sure you want to sync all the files ? (N/y) "
    read ok
    if [ "$ok" == "y" ]; then
        echo "Fine! Start in:"
        for i in `seq 5`; do
            echo -n "$((6 - i)) "
            sleep 1
        done
        echo "Let's go!"
    else
        echo "Abort!"
        exit 0
    fi
}

sync_function () {
    echo "cp ${qemudir}/${src_prefix}/${1} ${dst_prefix}/${1}"
    mkdir -p $(dirname "${dst_prefix}/${1}")
    cp -f "${qemudir}/${src_prefix}/${1}" "${dst_prefix}/${1}"
}

if [ ${cmd} == "sync" ]; then
    sync_warning
fi

while read f; do
    if [ ${f:0:1} == '#' ]; then
        src_prefix=`echo ${f:1} | awk -F ':' '{print $1}'`
        dst_prefix=`echo ${f:1} | awk -F ':' '{print $2}'`
        continue
    fi
    if [ ${cmd} == "diff" ]; then
        diff_function ${f} ${src_prefix} ${dst_prefix}
    elif [ ${cmd} == "sync" ]; then
        sync_function ${f} ${src_prefix} ${dst_prefix}
    fi
done < qemu-files.lst
