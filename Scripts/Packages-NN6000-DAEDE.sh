#!/usr/bin/env bash
set -Eeuo pipefail

: "${GITHUB_WORKSPACE:?GITHUB_WORKSPACE must point to the local OpenWRT-CI checkout}"

DAEDE_DIR="$GITHUB_WORKSPACE/vendor/daede"
AGENT_HUB_DIR="$GITHUB_WORKSPACE/vendor/agent-hub"

# Remove same-name feed entries so the vendored versions are unambiguous.
for feed_package in \
	../feeds/packages/net/dae \
	../feeds/packages/net/daed \
	../feeds/luci/applications/luci-app-dae \
	../feeds/luci/applications/luci-app-daed \
	./feeds/packages/dae \
	./feeds/packages/daed \
	./feeds/luci/luci-app-dae \
	./feeds/luci/luci-app-daed; do
	rm -rf "$feed_package"
done

for package_name in dae daed luci-app-daede vmlinux-btf; do
	if [[ ! -d "$DAEDE_DIR/$package_name" ]]; then
		echo "Missing vendored package: $DAEDE_DIR/$package_name" >&2
		exit 1
	fi
	rm -rf "./$package_name"
	cp -a "$DAEDE_DIR/$package_name" "./$package_name"
done

for package_name in picoclaw nullclaw zeroclaw agent-hub luci-app-agent-hub; do
	if [[ ! -d "$AGENT_HUB_DIR/$package_name" ]]; then
		echo "Missing vendored package: $AGENT_HUB_DIR/$package_name" >&2
		exit 1
	fi
	rm -rf "./$package_name"
	cp -a "$AGENT_HUB_DIR/$package_name" "./$package_name"
done

# OpenClash is kept on the same dev branch used by the upstream CI project.
find ../feeds/luci/ ../feeds/packages/ -maxdepth 3 -type d -iname '*openclash*' \
	-prune -exec rm -rf {} + 2>/dev/null || true
# `scripts/feeds install -a` can leave a same-name symlink under
# package/feeds even after the source feed directory is removed. Remove those
# links before copying the vendored package so Kconfig sees one definition.
rm -rf ./feeds/luci/luci-app-openclash \
	./feeds/packages/luci-app-openclash

OPENCLASH_DIR=$(mktemp -d "$PWD/.openclash.XXXXXX")
trap 'rm -rf "$OPENCLASH_DIR"' EXIT
git clone --depth=1 --single-branch --branch dev \
	'https://github.com/vernesong/OpenClash.git' "$OPENCLASH_DIR"

OPENCLASH_MAKEFILE=$(find "$OPENCLASH_DIR" -mindepth 1 -maxdepth 3 \
	-type f -path '*/luci-app-openclash/Makefile' -print -quit)
OPENCLASH_PACKAGE=${OPENCLASH_MAKEFILE%/Makefile}
if [[ -z "$OPENCLASH_PACKAGE" ]]; then
	echo 'OpenClash package directory was not found' >&2
	exit 1
fi
rm -rf ./luci-app-openclash
cp -a "$OPENCLASH_PACKAGE" ./luci-app-openclash

copy_package_from_repo() {
	local package_repo=$1
	local package_branch=$2
	local package_pattern=$3
	local package_destination=$4
	local package_checkout
	local package_source

	package_checkout=$(mktemp -d "$PWD/.package.XXXXXX")
	git clone --depth=1 --single-branch --branch "$package_branch" \
		"$package_repo" "$package_checkout"
	if [[ -f "$package_checkout/Makefile" ]]; then
		package_source="$package_checkout"
	else
		package_source=$(find "$package_checkout" -mindepth 1 -maxdepth 3 \
			-type d -iname "$package_pattern" -print -quit)
	fi
	if [[ -z "$package_source" ]]; then
		echo "Package directory was not found: $package_pattern" >&2
		rm -rf "$package_checkout"
		exit 1
	fi
	rm -rf "./$package_destination"
	mkdir -p "./$package_destination"
	rsync -a \
		--exclude='.git' \
		--exclude='.github' \
		--exclude='.dev' \
		--exclude='.vscode' \
		"$package_source/" "./$package_destination/"
	rm -rf "$package_checkout"
}

copy_package_from_repo \
	'https://github.com/eamonxg/luci-theme-aurora.git' master \
	'luci-theme-aurora' luci-theme-aurora
copy_package_from_repo \
	'https://github.com/eamonxg/luci-app-aurora-config.git' master \
	'luci-app-aurora-config' luci-app-aurora-config
