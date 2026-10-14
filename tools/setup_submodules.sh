#!/bin/sh
# setup_submodules.sh -- a fresh clone's third_party/: the upstream submodules, then our patch series on
# pcrecomp and MojoShader (third_party/patches/README.md: the submodules point at patched commits that
# exist only as these patches). One summary line; details in logs/setup_submodules.log.
cd "$(dirname "$0")/.." || exit 1
mkdir -p logs
L=logs/setup_submodules.log
: >"$L"
git submodule update --init third_party/d3d8to9 third_party/xboxrecomp >>"$L" 2>&1 || {
  echo "setup_submodules: FAIL (d3d8to9/xboxrecomp; $L)"
  exit 1
}
patched() { # $1 name, $2 upstream base
  d=third_party/$1
  git submodule init "$d" >>"$L" 2>&1
  if [ ! -e "$d/.git" ]; then
    url=$(git config -f .gitmodules "submodule.$d.url")
    rm -rf "$d" && git clone -q "$url" "$d" >>"$L" 2>&1 || return 1
  fi
  git -C "$d" checkout -q "$2" >>"$L" 2>&1 &&
    git -C "$d" -c user.name=setup -c user.email=setup@localhost am -q "$PWD/third_party/patches/$1"/*.patch >>"$L" 2>&1
}
patched pcrecomp 1f49cea || {
  echo "setup_submodules: FAIL (pcrecomp; $L)"
  exit 1
}
patched mojoshader ad5dff8 || {
  echo "setup_submodules: FAIL (mojoshader; $L)"
  exit 1
}
echo "setup_submodules: ok (d3d8to9, xboxrecomp; pcrecomp and mojoshader with their patches)"
