if [ -f update_ver.sh ]; then
    source update_ver.sh
fi

file_name=`git log origin/master -1 --pretty=format:"%cd_%h" --date=short`
#git archive --format=tar --verbose origin/master -- ./ | gzip > "./outdir/aic_wifi_for_1.xsdk-${file_name}.tar.gz"
git archive --prefix=aic8800/host/common/src/ --add-file=./host/common/src/co_version.c --prefix=aic8800/ --format=tar --verbose origin/master | gzip > "./outdir/aic_wifi_f133-sdk-${file_name}.tar.gz"
