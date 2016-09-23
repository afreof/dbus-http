#!/bin/bash -e

mkdir -p build/m4
autoreconf --force --install --symlink
