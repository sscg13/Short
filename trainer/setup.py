from setuptools import setup
from torch.utils.cpp_extension import CppExtension, BuildExtension
import glob
import os
import shutil
import subprocess
import sys


def find_tool(patterns):
    for pattern in patterns:
        hits = sorted(glob.glob(pattern), reverse=True)
        if hits:
            return hits[0]
    return None


def locate_clang_cl():
    override = os.environ.get("NNUE_CLANG_CL")
    if override:
        return override if os.path.isfile(override) else None
    found = shutil.which("clang-cl")
    if found:
        return found
    return find_tool([
        r"C:\msys64\mingw64\bin\clang-cl.exe",
        r"C:\msys64\clang64\bin\clang-cl.exe",
        r"C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\clang-cl.exe",
        r"C:\Program Files (x86)\Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\clang-cl.exe",
    ])


def locate_link():
    found = shutil.which("link.exe")
    if found and "MSVC" in found.upper():
        return found
    msvc = find_tool([
        r"C:\Program Files\Microsoft Visual Studio\2022\Preview\VC\Tools\MSVC\14.*\bin\HostX64\x64",
        r"C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\HostX64\x64",
    ])
    if not msvc:
        return None
    link = os.path.join(msvc, "link.exe")
    return link if os.path.isfile(link) else None


class BuildExtensionClang(BuildExtension):
    """Build the extension with clang-cl on Windows when available.

    Falls back to the stock MSVC path otherwise. The F16C shim is required
    because /arch:AVX2 implies __F16C__, and torch's Half.h then calls
    MSVC-only scalar converters that clang-cl does not provide.
    """

    def build_extension(self, ext):
        clang = sys.platform == "win32" and locate_clang_cl()
        if not clang or self.compiler.compiler_type != "msvc":
            if sys.platform == "win32" and not clang:
                print("nnue_extension: clang-cl not found; falling back to MSVC")
            super().build_extension(ext)
            return

        print(f"nnue_extension: building {ext.name} with {clang}")
        import sysconfig

        shim = os.path.abspath(os.path.join(os.path.dirname(__file__), "f16c_shim.h"))
        msvc_dir = find_tool([
            r"C:\Program Files\Microsoft Visual Studio\2022\Preview\VC\Tools\MSVC\14.*",
            r"C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*",
        ])
        sdk = find_tool([r"C:\Program Files (x86)\Windows Kits\10\Include\10.*"])
        include_dirs = list(ext.include_dirs or []) + [
            sysconfig.get_paths()["include"],
            os.path.join(msvc_dir, "include"),
            os.path.join(sdk, "ucrt"),
            os.path.join(sdk, "um"),
            os.path.join(sdk, "shared"),
        ]
        defines = [f"/D{n}" if v is None else f"/D{n}={v}"
                   for n, v in (ext.define_macros or [])]
        # torch adds these inside BuildExtension.build_extensions(), after our
        # hook has already run, so provide them here.
        modname = ext.name.split(".")[-1]
        defines += [f"/DTORCH_EXTENSION_NAME={modname}", "/DTORCH_API_INCLUDE_EXTENSION_H"]

        objects = []
        for src in ext.sources:
            obj = os.path.join(self.build_temp,
                               os.path.splitext(os.path.basename(src))[0] + ".obj")
            os.makedirs(os.path.dirname(obj) or ".", exist_ok=True)
            cmd = [clang, "/c", "/nologo", "/O2", "/arch:AVX2", "/std:c++17",
                   "/EHsc", "/MD", f"/FI{shim}", f"/Fo{obj}"] + defines
            cmd += [f"-I{d}" for d in include_dirs] + [src]
            print("nnue_extension:", " ".join(cmd))
            subprocess.check_call(cmd)
            objects.append(obj)

        linker = locate_link()
        if linker is None:
            raise RuntimeError("nnue_extension: link.exe not found for clang-cl build")

        py_libdir = os.path.join(sys.base_prefix, "libs")
        torch_libdir = os.path.abspath(os.path.join(
            os.path.dirname(__import__("torch").__file__), "lib"))
        sdklib = find_tool([r"C:\Program Files (x86)\Windows Kits\10\Lib\10.*"])
        libdirs = [
            py_libdir,
            torch_libdir,
            os.path.join(msvc_dir, "lib", "x64"),
            os.path.join(sdklib, "um", "x64"),
            os.path.join(sdklib, "ucrt", "x64"),
        ]
        out = self.get_ext_fullpath(ext.name)
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        init = "PyInit_" + ext.name.split(".")[-1]
        libs = [n if n.lower().endswith(".lib") else n + ".lib"
                for n in (ext.libraries or [])]
        cmd = [linker, "/nologo", "/DLL", f"/OUT:{out}", "/INCREMENTAL:NO"] + objects + [
            f"/LIBPATH:{d}" for d in libdirs
        ] + libs + [f"/EXPORT:{init}"]
        print("nnue_extension: linking:", " ".join(cmd))
        subprocess.check_call(cmd)


if sys.platform == "win32":
    compile_args = ["/O2", "/arch:AVX2", "/std:c++17"]
    link_args = []
else:
    compile_args = ["-O3", "-std=c++17", "-march=native"]
    link_args = []


setup(
    name="nnue_extension",
    ext_modules=[
        CppExtension(
            name="nnue_extension",
            sources=["nnue_extension.cpp"],
            extra_compile_args={"cxx": compile_args},
            extra_link_args=link_args,
        )
    ],
    cmdclass={"build_ext": BuildExtensionClang},
)
