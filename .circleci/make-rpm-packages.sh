#!/usr/bin/env bash

set -eux

echo "PARAM_RELEASE: $PARAM_RELEASE"
echo "PARAM_DIST: $PARAM_DIST"

if test "x$PARAM_RELEASE" = "x"; then
    echo "Env variable PARAM_RELEASE is not set! For example PARAM_RELEASE=8, for CentOS 8"
    exit 1
elif test "x$PARAM_DIST" = "x"; then
    echo "Env variable PARAM_DIST is not set! For example PARAM_DIST=centos"
    exit 1
fi

export DIST_DIR=build

# XXX: we should NOT have to do that here, they should be in the
# spec as BuildRequires
yum install -y make gcc

adduser varnish
chown -R varnish:varnish /workspace

cd /varnish-cache
rm -rf $DIST_DIR
mkdir $DIST_DIR


echo "Untar redhat..."
tar xavf /workspace/redhat.tar.gz -C $DIST_DIR

echo "Untar orig..."
tar xavf /workspace/varnish-*.tar.gz -C $DIST_DIR --strip 1

echo "Build Packages..."
if [ $PARAM_RELEASE = 8 ]; then
    yum config-manager --set-enabled PowerTools
fi
# use python3
sed -i '1 i\%global __python %{__python3}' "$DIST_DIR"/redhat/varnish.spec
if [ -e /workspace/.is_weekly ]; then
    WEEKLY='.weekly'
else
    WEEKLY=
fi
VERSION=$("$DIST_DIR"/configure --version | awk 'NR == 1 {print $NF}')$WEEKLY

cp -r -L "$DIST_DIR"/redhat/* "$DIST_DIR"/
tar zcf "$DIST_DIR.tgz" --exclude "$DIST_DIR/redhat" "$DIST_DIR"/

RPMVERSION="$VERSION"

RESULT_DIR="rpms"
CUR_DIR="$(pwd)"

rpmbuild() {
    command rpmbuild \
        --define "_smp_mflags -j10" \
        --define "_sourcedir $CUR_DIR" \
        --define "_srcrpmdir $CUR_DIR/${RESULT_DIR}" \
        --define "_rpmdir $CUR_DIR/${RESULT_DIR}" \
        --define "versiontag ${RPMVERSION}" \
        --define "releasetag 0.0" \
        --define "srcname $DIST_DIR" \
        --define "nocheck 1" \
        "$@"
}

yum-builddep -y "$DIST_DIR"/redhat/varnish.spec
rpmbuild -bs "$DIST_DIR"/redhat/varnish.spec
rpmbuild --rebuild "$RESULT_DIR"/varnish-*.src.rpm

echo "Prepare the packages for storage..."
mkdir -p packages/rpm/$PARAM_DIST/$PARAM_RELEASE/
mv rpms/*/*.rpm packages/rpm/$PARAM_DIST/$PARAM_RELEASE/
