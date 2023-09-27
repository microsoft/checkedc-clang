# 3C: Semi-automated conversion of C code to Checked C

The 3C (**C**hecked-**C**-**C**onvert) software facilitates conversion
of existing C code to Checked C by automatically inferring as many
Checked C annotations as possible and guiding the developer through
the process of assigning the remaining ones. 3C aims to provide the
first feasible way for organizations with large legacy C codebases
(that they don't want to drop everything to rewrite in a better
language) to comprehensively verify their code's spatial memory
safety.

This document describes 3C in general. Here are the [build
instructions](INSTALL.md). The inference logic is implemented in the
[`clang/lib/3C` directory](../../../lib/3C) in the form of a library
that can potentially be used in multiple contexts.

There are two ways to
use 3C.
- The first method is using the command line tool `3c` 
[`clang/tools/3c` directory](../../../tools/3c); its usage is
documented in [the readme there](../../../tools/3c/README.md).
- The second method is to use a vscode extension along with
clangd lsp plugin. Steps to build and use the plugin can be
found [here](https://3clsp.github.io/). Note that 3CLSP is based on
an older fork and therefore won't have the latest changes to 3C.

## What 3C users should know about the development process

Checked C development as a whole is led by Microsoft in the
https://github.com/microsoft/checkedc-clang repository. 3C is included
in the Checked C codebase. Initially 3C development was led by [Correct
Computation, Inc.](https://correctcomputation.com/). As of now, 3C is
integrated into [Microsoft's](https://github.com/microsoft/checkedc-clang)
checkedc-clang repo and all furthur development will be in
[Microsoft's](https://github.com/microsoft/checkedc-clang) repo.

Issues and pull requests related to 3C should be submitted to 
[Microsoft's](https://github.com/microsoft/checkedc-clang)
repository; see [CONTRIBUTING.md](CONTRIBUTING.md) for more
information.

As of March 2021, 3C is pre-alpha quality and we are just starting to
establish its public presence and processes.

As noted in the [setup instructions](INSTALL.md#basics), both 3C and
the Checked C compiler depend on the Checked C system headers, which
Microsoft maintains in [a subdirectory of a separate repository named
`checkedc`](https://github.com/microsoft/checkedc/tree/master/include).
