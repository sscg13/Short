from setuptools import setup
from torch.utils.cpp_extension import CppExtension, BuildExtension
import sys


if sys.platform == "win32":
    compile_args = ["/O2", "/std:c++17"]
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
    cmdclass={"build_ext": BuildExtension},
)
