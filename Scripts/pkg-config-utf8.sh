#!/bin/sh

tmp_file="${TMPDIR:-/tmp}/openwrt-pkg-config.$$"
trap 'rm -f "$tmp_file"' 0 1 2 3 15

"${STAGING_DIR_HOST}/bin/pkg-config.real" \
	--keep-system-cflags \
	--keep-system-libs \
	--define-variable=prefix="${STAGING_PREFIX:-}" \
	--define-variable=prefix_host="${STAGING_DIR_HOST}" \
	--define-variable=prefix_hostpkg="${STAGING_DIR_HOSTPKG:-}" \
	--define-variable=exec_prefix="${STAGING_PREFIX:-}" \
	--define-variable=bindir="${STAGING_PREFIX:-}/bin" \
	${PKG_CONFIG_EXTRAARGS:-} "$@" >"$tmp_file"
status=$?

# pkgconf escapes each byte of non-ASCII absolute paths with a backslash.
# Remove only those extra backslashes so Meson can decode the UTF-8 path.
LC_ALL=C sed -e 's/\\\([\x80-\xff]\)/\1/g' "$tmp_file"
exit "$status"
