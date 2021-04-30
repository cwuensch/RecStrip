#!/bin/bash
# Copyright (c) 2000-2016 Synology Inc. All rights reserved.

source /pkgscripts/include/pkg_util.sh

package="LogExtractor"
version="1.0.0000"
displayname="LogExtractor"
maintainer="Christian Wünsch"
arch="$(pkg_get_unified_platform)"
description="Extracts infos from Topfield inf and continuity errors from a RecStrip logfile."
[ "$(caller)" != "0 NULL" ] && return 0
pkg_dump_info
