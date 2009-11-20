DEB_EXTRA_CONFIGURE_FLAGS := --disable-runtime-cpudetection --disable-vidix --disable-tga --disable-pnm --disable-ossaudio --disable-gui --disable-live --disable-directfb
export DEB_EXTRA_CONFIGURE_FLAGS

SHELL := /bin/bash

all: build

mt: build-mt

mplayer/debian/changelog: update-mplayer
	rm -f mplayer/debian/changelog
	cd mplayer && dch --create --empty --package mplayer -v 2:1.0~rc5++svn$(shell date +%Y%m%d)-r$(shell cd mplayer && LC_ALL=C git log -1 origin/master 2>/dev/null | sed -n '/git-svn-id/s/.*@\([0-9]\+\) .*/\1/p') "Daily build"

update: update-mplayer update-ffmpeg update-dvdnav
update-mt: update-mplayer update-ffmpeg-mt update-dvdnav

copy: copy-dvdnav
copy-mt: copy-ffmpeg-mt copy-dvdnav

build: build-stamp
build-stamp: update copy mplayer/debian/changelog
	cd mplayer && fakeroot debian/rules binary
	touch build-stamp

build-mt: build-mt-stamp
build-mt-stamp: update-mt copy-mt mplayer/debian/changelog
	cd mplayer && fakeroot debian/rules binary
	touch build-mt-stamp

update-mplayer: update-mplayer-stamp
update-mplayer-stamp:
	cd mplayer && git pull --rebase && git gc
	touch update-mplayer-stamp

update-ffmpeg: update-ffmpeg-stamp
update-ffmpeg-stamp:
	cd mplayer/ffmpeg && git pull --rebase && git gc
	touch update-ffmpeg-stamp

update-ffmpeg-mt: update-ffmpeg-mt-stamp
update-ffmpeg-mt-stamp:
	cd ffmpeg-mt && git pull --rebase && git gc
	touch update-ffmpeg-mt-stamp

update-dvdnav: update-dvdnav-stamp
update-dvdnav-stamp:
	cd dvdnav && git svn fetch -r HEAD && git svn rebase -l && git gc
	touch update-dvdnav-stamp

copy-dvdnav: copy-dvdnav-stamp
copy-dvdnav-stamp: update-dvdnav-stamp
	cp -r  dvdnav/libdvdread/src/ mplayer/libdvdread4
	cp -r  dvdnav/libdvdnav/src/ mplayer/libdvdnav
	touch copy-dvdnav-stamp

source:
	git clone git://git.mplayerhq.hu/mplayer
	cd mplayer && git clone git://git.mplayerhq.hu/ffmpeg
	cd mplayer/ffmpeg && git clone git://git.mplayerhq.hu/libswscale
	git svn clone -r HEAD svn://svn.mplayerhq.hu/dvdnav/trunk dvdnav
	git clone git://gitorious.org/~astrange/ffmpeg/ffmpeg-mt.git

depend:
	sudo aptitude install libasound2-dev libxv-dev libfontconfig1-dev yasm libtheora-dev
	#libpng12-dev libungif4-dev libmp3lame-dev libfaac-dev libmad0-dev

debug:
	cd mplayer && ./configure $(DEB_BUILD_OPTIONS) --enable-debug --disable-liba52-internal && make

clean:
	cd mplayer && fakeroot debian/rules clean
	rm -rf mplayer/{libdvdread4,libdvdnav}
	rm -f *-stamp

.PHONY: all clean
