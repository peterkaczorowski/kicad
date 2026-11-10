#!/usr/bin/env python3
"""Build an installable client with a descriptor set from this KiCad checkout."""
import argparse
import pathlib
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--protoc', default='protoc')
args = parser.parse_args()
source = pathlib.Path(__file__).resolve().parent
proto = source.parent / 'proto'
output = args.output.resolve()
if output == source or source in output.parents:
    parser.error('Use a build directory outside the source package')
output.mkdir(parents=True, exist_ok=True)
package = output / 'kipy_schematic'
package.mkdir(exist_ok=True)
shutil.copy2(source / 'kipy_schematic' / '__init__.py', package)
subprocess.run([args.protoc, f'--proto_path={proto}', '--include_imports',
                f'--descriptor_set_out={package / "_protocol.pb"}',
                'common/commands/editor_commands.proto',
                'schematic/schematic_commands.proto'], check=True)
(output / 'pyproject.toml').write_text('''[build-system]
requires = ["setuptools>=68"]
build-backend = "setuptools.build_meta"

[project]
name = "kipy-schematic"
version = "0.1.0"
requires-python = ">=3.10"
dependencies = ["kicad-python>=0.8,<0.9", "protobuf>=5.29,<8"]

[tool.setuptools]
packages = ["kipy_schematic"]

[tool.setuptools.package-data]
kipy_schematic = ["_protocol.pb"]
''')
print(f'Client prepared in {output}. Install with: python -m pip install {output}')
