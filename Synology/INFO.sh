#!/bin/bash
# Copyright (c) 2000-2016 Synology Inc. All rights reserved.

source /pkgscripts/include/pkg_util.sh

package="RecStrip"
version="2.4a"
displayname="RecStrip package"
maintainer="Christian Wünsch"
arch="$(pkg_get_unified_platform)"
description="RecStrip enables conversion, examination and size reduction of Topfield TMS recordings."
[ "$(caller)" != "0 NULL" ] && return 0
pkg_dump_info
