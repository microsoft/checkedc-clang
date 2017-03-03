
@rem  Set up sources, build clang, and test clang.
@rem
@rem  This script assumes the following environment varibles have been set
@rem  before it is invoked.
@rem
@rem  BUILD_SOURCESDIRECTORY: the directory where the LLVM source will be
@rem                          placed.
@rem  BUILD_BINARIES: the directory where the object directory will be placed.
@rem  BUILDCONFIGURATION: The clang version to build.  Must be one of Debug, 
@rem                      Release,ReleaseWithDebInfo.
@rem  TEST_SUITE: the test suite to run.  Must be one of:
@rem              - CheckedC: run the Checked C regression tests.
@rem              - CheckedC: run the Checked C and clang regression tets.
@rem              - CheckedC_LLVM: run the Checked C and LLVM regression tests
@rem               
@rem
@rem  The following variable may be optionally set:
@rem  BUILDOS: The OS that we building upon.  May be one of Windows or WSL.
@rem           WSL stands for Windows Subsystem for Linux.  Defaults to
@rem           X86.
@rem  TEST_TARGET_ARCH: the target architecuture on which testing will be
@rem                    run.  May be one of X86 or AMD64.  Defaults to X86.
@rem
@rem  Because the Checked C clang build involves 3 repositories that may
@rem  be in varying states of consistency, the following variables can
@rem  optionally be set to specify branches or commits to use for testing.
@rem
@rem  CHECKEDC_BRANCH: defaults to master
@rem  CHECKEDC_COMMIT: defaults to HEAD
@rem  LLVM_BRANCH: defaults to master
@rem  LLVM_BRANCH: defaults to HEAD
@rem  CLANG_BRANCH: If not set, uses BUILD_SOURCEBRANCHNAME if defined.
@rem                If BUILD_SOURCEBRANCHNAME is not defined, defaults
@rem                 to master
@rem  CLANG_COMMIT: If not set, uses BUILD_SOURCEVERSION if defined.
@rem                If BUILD_SOURCEVERSION is not default, defaults to
@rem                HEAD.

@setlocal
@set OLD_DIR=%CD%
@call .\config-vars.bat
if ERRORLEVEL 1 (goto cmdfailed)

echo.
echo.Setting up files.

call .\setup-files.bat
if ERRORLEVEL 1 (goto cmdfailed)

echo.
echo.Running cmake

call .\run-cmake.bat
if ERRORLEVEL 1 (goto cmdfailed)

echo.
echo.Building clang

call .\build-clang.bat
if ERRORLEVEL 1 (goto cmdfailed)

echo.
echo.Testing clang

call .\test-clang.bat
if ERRORLEVEL 1 (goto cmdfailed)

:succeeded
  cd %OLD_DIR%
  exit /b 0

:cmdfailed
  echo.Build and test failed.
  cd %OLD_DIR%
  exit /b 1


