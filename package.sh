#!/bin/sh
# Builds the Toji HPKG from dist/ (run ./build.sh first).
# The package is written to the repository root; set REVISION to override the revision (default 1).
set -e
cd "$(dirname "$0")"

APP=dist/Toji
RDEF=src/app/Toji.rdef
[ -f "$APP" ] || { echo "$APP not found, run ./build.sh first" >&2; exit 1; }

# The package requires the mupdf1.28 package. A binary that has MuPDF built in (./build.sh without the
# mupdf1.28_devel package installed) would also need the libraries MuPDF uses, which the package does not list.
grep -q 'libmupdf1.28.so' $APP || {
	echo "$APP does not use the shared MuPDF; install mupdf1.28_devel (https://kiri.sen-labs.org/x86_64) and rebuild" >&2
	exit 1
}

# The translations are bound into the program's resources (not a build step of make): a binary without them is not translated. Binding again
# replaces what is there, so a build that did it already (CI) is not harmed.
make -C src bindcatalogs >/dev/null || { echo "binding the catalogs failed" >&2; exit 1; }

# version from the app_version resource, a development build becomes a pre-release, a beta is 1.0.0~beta1
major=$(sed -n 's/^[[:space:]]*major[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
middle=$(sed -n 's/^[[:space:]]*middle[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
minor=$(sed -n 's/^[[:space:]]*minor[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
VERSION="$major.$middle.$minor"
grep -q 'variety[[:space:]]*=[[:space:]]*B_APPV_DEVELOPMENT' $RDEF && VERSION="$VERSION~dev"
if grep -q 'variety[[:space:]]*=[[:space:]]*B_APPV_BETA' $RDEF; then
	internal=$(sed -n 's/^[[:space:]]*internal[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
	VERSION="$VERSION~beta${internal:-1}"
fi
VERSION="$VERSION-${REVISION:-1}"
ARCH=$(getarch)

# The app finds the start page next to itself, so it goes into apps/Toji.
STAGE=package-build
rm -rf $STAGE
mkdir -p $STAGE/apps/Toji $STAGE/data/deskbar/menu/Applications
cp -a $APP dist/license $STAGE/apps/Toji/
# the texts of the licenses of what is in the package (AGPL for the program, MIT for lib/, CC BY 4.0 for the artwork)
cp -a LICENSES/. $STAGE/apps/Toji/license/
cp -a lib/LICENSE $STAGE/apps/Toji/license/MIT-lib-LICENSE
mkdir -p $STAGE/data/licenses
cp -a dist/license/AGPL-3.0 "$STAGE/data/licenses/GNU AGPL v3"
mkdir $STAGE/apps/Toji/docs
# the start page (shown when no file is given) and the user guide (Help), built from docs/guide by CI or by build.sh there; the
# package is made without them if they are not there
for guide in docs/guide/build/toji-start.pdf docs/guide/build/toji-guide.pdf docs/guide/build/toji-guide.epub; do
	[ -f "$guide" ] && cp -a "$guide" $STAGE/apps/Toji/docs/
done
cp -a images/toji-logo.png $STAGE/apps/Toji/docs/
mkdir $STAGE/apps/Toji/sounds
cp -a sounds/pageturn.wav $STAGE/apps/Toji/sounds/
ln -s ../../../../apps/Toji/Toji $STAGE/data/deskbar/menu/Applications/Toji
sed -e "s|@VERSION@|$VERSION|g" -e "s|@ARCH@|$ARCH|g" package/PackageInfo.in > $STAGE/.PackageInfo

PACKAGE=toji-$VERSION-$ARCH.hpkg
rm -f $PACKAGE
package create -C $STAGE $PACKAGE
rm -rf $STAGE
echo "created $PACKAGE"
