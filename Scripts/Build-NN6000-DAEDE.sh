#!/usr/bin/env bash
set -Eeuo pipefail

CI_ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SOURCE_DIR=${SOURCE_DIR:-"$CI_ROOT/../immortalwrt-local"}
JOBS=${JOBS:-$(nproc)}
HOST_DEPS_DIR=${HOST_DEPS_DIR:-/tmp/openwrt-host-deps/root}

# Ruby 4.0's build scripts inspect paths while their generated miniruby is
# running. Keep a UTF-8 locale so the local Chinese workspace path is valid.
export LANG=C.UTF-8
export LANGUAGE=C.UTF-8
export LC_ALL=C.UTF-8
export LC_CTYPE=C.UTF-8

# Ruby 4.0's generated config.status is not safe when Autoconf folds a
# multibyte path. Keep the real checkout where it is, but build through an
# ASCII symlink when the workspace path contains non-ASCII characters.
if LC_ALL=C printf '%s' "$SOURCE_DIR" | grep -q '[^ -~]'; then
	ASCII_SOURCE_DIR=${ASCII_SOURCE_DIR:-/tmp/openwrt-nn6000-build}
	SOURCE_REAL=$(readlink -f "$SOURCE_DIR")
	if [[ -L "$ASCII_SOURCE_DIR" ]]; then
		[[ "$(readlink -f "$ASCII_SOURCE_DIR")" == "$SOURCE_REAL" ]] || {
			echo "ASCII_SOURCE_DIR points to a different source tree: $ASCII_SOURCE_DIR" >&2
			exit 1
		}
	elif [[ -e "$ASCII_SOURCE_DIR" ]]; then
		echo "ASCII_SOURCE_DIR already exists and is not the source symlink: $ASCII_SOURCE_DIR" >&2
		exit 1
	else
		ln -s "$SOURCE_REAL" "$ASCII_SOURCE_DIR"
	fi
	SOURCE_DIR=$ASCII_SOURCE_DIR
fi

if [[ -d "$HOST_DEPS_DIR/usr/bin" ]]; then
	export PATH="$HOST_DEPS_DIR/usr/bin:$PATH"
	export CPATH="$HOST_DEPS_DIR/usr/include${CPATH:+:$CPATH}"
	export LIBRARY_PATH="$HOST_DEPS_DIR/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}"
	export LD_LIBRARY_PATH="$HOST_DEPS_DIR/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
	export PYTHONPATH="$HOST_DEPS_DIR/usr/lib/python3.10:$HOST_DEPS_DIR/usr/lib/python3.11${PYTHONPATH:+:$PYTHONPATH}"
fi

for command_name in git make gcc g++ rsync bc perl python3 gawk patch; do
	if ! command -v "$command_name" >/dev/null 2>&1; then
		echo "Missing host command: $command_name" >&2
		exit 1
	fi
done

if [[ ! -f "$SOURCE_DIR/Makefile" ]]; then
	echo "ImmortalWrt source not found: $SOURCE_DIR" >&2
	exit 1
fi

export GITHUB_WORKSPACE="$CI_ROOT"
export WRT_CONFIG=NN6000-DAEDE
export WRT_THEME=aurora
export WRT_NAME=OWRT-NN6000
export WRT_SSID=OWRT
export WRT_WORD=12345678
export WRT_IP=192.168.100.1
export WRT_PW=none
export WRT_REPO=https://github.com/VIKINGYFY/immortalwrt.git
export WRT_BRANCH=main
export WRT_SOURCE=VIKINGYFY/immortalwrt
export WRT_PACKAGE=
export WRT_TEST=false

cd "$SOURCE_DIR"
export TOPDIR="$SOURCE_DIR"
make_openwrt() {
	make TOPDIR="$SOURCE_DIR" "$@"
}
echo "Source: $(git rev-parse --short HEAD)"
echo "Target: Link NN6000 v2"
echo "Jobs: $JOBS"

# util-linux leaves a uuid.pc file in the OpenWrt host staging area. Supply
# the matching headers and static library when the host package is unpacked
# under HOST_DEPS_DIR instead of installed system-wide.
if [[ -d "$HOST_DEPS_DIR/usr/include/uuid" && -f "$HOST_DEPS_DIR/usr/lib/x86_64-linux-gnu/libuuid.a" ]]; then
	mkdir -p staging_dir/host/include/uuid staging_dir/host/lib
	cp -a "$HOST_DEPS_DIR/usr/include/uuid/." staging_dir/host/include/uuid/
	cp -a "$HOST_DEPS_DIR/usr/lib/x86_64-linux-gnu/libuuid.a" staging_dir/host/lib/
	export libuuid_CFLAGS="-I$SOURCE_DIR/staging_dir/host/include/uuid"
	export libuuid_LIBS="-L$SOURCE_DIR/staging_dir/host/lib -luuid -pthread"
fi

prepare_pkg_config() {
	local pkg_config="$SOURCE_DIR/staging_dir/host/bin/pkg-config"
	if [[ -x "$pkg_config" && -x "$SOURCE_DIR/staging_dir/host/bin/pkg-config.real" ]]; then
		cp -f "$CI_ROOT/Scripts/pkg-config-utf8.sh" "$pkg_config"
		chmod +x "$pkg_config"
	fi
}

prepare_host_tool_aliases() {
	local aclocal="$SOURCE_DIR/staging_dir/host/bin/aclocal"
	local aclocal_source="$SOURCE_DIR/tools/automake/files/aclocal"
	local automake_root="$SOURCE_DIR/build_dir/host/automake-1.18.1"
	local automake_share="$SOURCE_DIR/staging_dir/host/share/automake-1.18"
	local aclocal_share="$SOURCE_DIR/staging_dir/host/share/aclocal-1.18"
	if [[ ! -e "$aclocal" && -x "$aclocal_source" && -x "$SOURCE_DIR/staging_dir/host/bin/aclocal.real" ]]; then
		cp -f "$aclocal_source" "$aclocal"
		chmod +x "$aclocal"
	fi
	if [[ -d "$automake_root/lib/Automake" ]]; then
		mkdir -p "$automake_share/Automake" "$automake_share/am" "$aclocal_share"
		cp -af "$automake_root/lib/Automake/." "$automake_share/Automake/"
		cp -af "$automake_root/lib/am/." "$automake_share/am/"
		cp -af "$automake_root/m4/." "$aclocal_share/"
	fi
	if [[ -f "$automake_root/bin/automake" && ! -e "$SOURCE_DIR/staging_dir/host/bin/automake" ]]; then
		cp -f "$automake_root/bin/automake" "$SOURCE_DIR/staging_dir/host/bin/automake"
		chmod +x "$SOURCE_DIR/staging_dir/host/bin/automake"
	fi
	if [[ -x "$SOURCE_DIR/staging_dir/host/bin/automake" ]]; then
		for api in 11 12 13 14 15 16 17 18; do
			ln -sf automake "$SOURCE_DIR/staging_dir/host/bin/automake-1.$api"
		done
	fi
}

prepare_host_yaml() {
	local yaml_dir="$SOURCE_DIR/feeds/packages/libs/yaml"
	if [[ -f "$yaml_dir/Makefile" ]]; then
		make -C "$yaml_dir" TOPDIR="$SOURCE_DIR" IS_PACKAGE_BUILD=1 host-compile
	fi
}

apply_local_patches() {
	local ruby_makefile="$SOURCE_DIR/feeds/packages/lang/ruby/Makefile"
	local ruby_patch="$CI_ROOT/Patches/NN6000/ruby-utf8-path.patch"
	if [[ -f "$ruby_makefile" && -f "$ruby_patch" ]] && ! grep -q 'CPP="\$(TARGET_CC) -E -P"' "$ruby_makefile"; then
		patch -d "$SOURCE_DIR" -p1 < "$ruby_patch"
	fi
}

if [[ "${SKIP_FEEDS:-0}" != 1 ]]; then
	./scripts/feeds update -a
	./scripts/feeds install -a
fi

rm -f .config
(cd package && bash "$CI_ROOT/Scripts/Packages-NN6000-DAEDE.sh")
apply_local_patches

# Remove any broken feed links/source paths recreated by feed installation.
# Keep the vendored package as the only OpenClash definition in the tree.
rm -rf \
	feeds/luci/applications/luci-app-openclash \
	feeds/packages/luci-app-openclash \
	package/feeds/luci/luci-app-openclash \
	package/feeds/packages/luci-app-openclash
rm -f feeds/luci.tmp/info/.packageinfo-applications_luci-app-openclash

# `scripts/feeds install -a` runs an early package scan before the vendored
# packages are injected. Remove all generated package-scan state so a deleted
# feed package cannot survive into the later defconfig scan. Recreate the
# complete generated-info directory because scan.mk also keeps dependency
# fragments there.
rm -f \
	tmp/.packageinfo \
	tmp/.config-package.in \
	tmp/.packagedeps \
	tmp/.packageauxvars \
	tmp/.packageusergroup
rm -rf tmp/info

# The DAEDE packages are copied into the tree after feed installation. Force
# OpenWrt to rescan package metadata so injected packages are visible to
# Kconfig during the first defconfig pass.
make_openwrt prepare-tmpinfo

if ! grep -q '^Source-Makefile: package/luci-app-openclash/Makefile$' tmp/.packageinfo; then
	echo 'The self-maintained OpenClash package was not found in package metadata' >&2
	exit 1
fi
if grep -q '^Source-Makefile: feeds/.*/luci-app-openclash/Makefile$' tmp/.packageinfo; then
	echo 'A feed OpenClash package remains in package metadata' >&2
	exit 1
fi

cat "$CI_ROOT/Config/NN6000-DAEDE.txt" \
	"$CI_ROOT/Config/GENERAL-NN6000-DAEDE.txt" > .config
bash "$CI_ROOT/Scripts/Settings.sh"

make_openwrt defconfig -j"$JOBS"
# dae/daed declare bpf-headers as a build dependency, but do not select the
# hidden LLVM build symbol in Kconfig. Keep the required host toolchain explicit.
if ! grep -q '^CONFIG_USE_LLVM_BUILD=y$' .config; then
	printf '%s\n' 'CONFIG_USE_LLVM_BUILD=y' >> .config
fi

for required_package in \
	luci-app-openclash \
	dae \
	daed \
	luci-app-daede \
	luci-app-agent-hub \
	agent-hub \
	picoclaw \
	nullclaw \
	zeroclaw; do
	if ! grep -Fqx "CONFIG_PACKAGE_${required_package}=y" .config; then
		echo "Required package was dropped from .config: ${required_package}" >&2
		exit 1
	fi
done

mkdir -p "$CI_ROOT/build"
cp -f .config "$CI_ROOT/build/NN6000-DAEDE.config"

if [[ "${DEFCONFIG_ONLY:-0}" == 1 ]]; then
	echo "Defconfig finished: $CI_ROOT/build/NN6000-DAEDE.config"
	exit 0
fi

make_openwrt tools/compile -j"$JOBS"
# Direct host-package calls below do not inherit OpenWrt's TARGET_PATH_PKG.
# Make the freshly built host tools (notably ccache) available explicitly.
export PATH="$SOURCE_DIR/staging_dir/host/bin:$PATH"
prepare_pkg_config
prepare_host_tool_aliases
prepare_host_yaml
make_openwrt download -j"$JOBS"
make_openwrt -j"$JOBS" || make_openwrt -j1 V=s

echo
echo "Build finished. Artifacts are under: $SOURCE_DIR/bin/targets/qualcommax/ipq60xx"
