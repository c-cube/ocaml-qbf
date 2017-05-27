#!/usr/bin/env sh

# default setttings
SWITCH='4.03.0+mingw32c'
OPAM_URL='https://github.com/fdopen/opam-repository-mingw/releases/download/0.0.0.1/opam32.tar.xz'
OPAM_ARCH=opam32

if [ "$PROCESSOR_ARCHITECTURE" != "AMD64" ] && \
       [ "$PROCESSOR_ARCHITEW6432" != "AMD64" ]; then
    OPAM_URL='https://github.com/fdopen/opam-repository-mingw/releases/download/0.0.0.1/opam32.tar.xz'
    OPAM_ARCH=opam32
fi

if [ $# -gt 0 ] && [ -n "$1" ]; then
    SWITCH=$1
fi

export OPAM_LINT="false"
export CYGWIN='winsymlinks:native'
export OPAMYES=1
# 'b' means that backtraces will be displayed on exceptions
export OCAMLRUNPARAM=b

set -eu

curl -fsSL -o "${OPAM_ARCH}.tar.xz" "${OPAM_URL}"
tar -xf "${OPAM_ARCH}.tar.xz"
"${OPAM_ARCH}/install.sh"

opam init -a default "https://github.com/fdopen/opam-repository-mingw.git" --comp "$SWITCH" --switch "$SWITCH"
eval $(opam config env)
ocaml_system="$(ocamlc -config | awk '/^system:/ { print $2 }')"
case "$ocaml_system" in
    *mingw64*)
        PATH="/usr/x86_64-w64-mingw32/sys-root/mingw/bin:${PATH}"
        export PATH
        ;;
    *mingw*)
        PATH="/usr/i686-w64-mingw32/sys-root/mingw/bin:${PATH}"
        export PATH
        ;;
    *)
        echo "ocamlc reports a dubious system: ${ocaml_system}. Good luck!" >&2
esac

# Now, the actual opam install, build and test
opam install ocamlfind ounit
eval $(opam config env)

cd "${APPVEYOR_BUILD_FOLDER}"
./configure --enable-quantor --enable-tests
make
make test

opam pin add -y .
opam remove qbf
[ -z "`ocamlfind query qbf`" ] || (echo "It uninstalled fine!" && exit 1)