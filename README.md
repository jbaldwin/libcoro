libcoro C++20 Coroutines
========================

[![CI](https://github.com/jbaldwin/libcoro/workflows/build/badge.svg)](https://github.com/jbaldwin/libcoro/workflows/build/badge.svg)
[![Coverage Status](https://coveralls.io/repos/github/jbaldwin/libcoro/badge.svg?branch=master)](https://coveralls.io/github/jbaldwin/libcoro?branch=master)
[![language][badge.language]][language]
[![license][badge.license]][license]

[badge.language]: https://img.shields.io/badge/language-C%2B%2B20-yellow.svg
[badge.license]: https://img.shields.io/badge/license-Apache--2.0-blue

[language]: https://en.wikipedia.org/wiki/C%2B%2B17
[license]: https://en.wikipedia.org/wiki/Apache_License

**libcoro** is licensed under the Apache 2.0 license.

# Background
Libcoro is a C++20 coroutine library.  So far most inspiration has been gleaned from [libcppcoro](https://github.com/lewissbaker/cppcoro) an amazing C++ coroutine library as well as Lewis Baker's great coroutine blog entries [https://lewissbaker.github.io/](Blog).  I would highly recommend anyone who is trying to learn the internals of C++20's coroutine implementation to read all of his blog entries, they are extremely insightful and well written.

# Goal
Libcoro is currently more of a learning experience for myself but ultimately I'd like to turn this into a great linux coroutine base library with an easy to use HTTP scheduler/server.

# Building
There is a root makefile with various commands to help make building and running tests on this project easier.

```bash
# Build targets
make debug|release-with-debug-info|release

# Run tests targets
make debug-test|release-with-debug-info-tests|release-tests

# Clean all builds.
make clean

# clang-format the code
make format
```
