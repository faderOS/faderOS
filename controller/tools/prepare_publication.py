#!/usr/bin/env python3
"""Build a reviewed standalone source archive without private workspace assets."""
import argparse
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parents[1]
FILES = ('CMakeLists.txt', 'LICENSE', 'README.md', 'CONTRIBUTING.md', 'CHANGELOG.md',
         'ATEM.md', 'VMIX.md', 'OBS.md', 'ROADMAP.md', '.gitignore',
         'test/requirements.txt', 'tools/embed_web.py', 'tools/generate_link_catalog.py', 'tools/prepare_publication.py')
TREES = ('src/link', 'src/platform', 'test/link', 'web', 'docs', '.github', 'examples', 'metadata')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    paths = {ROOT / name for name in FILES}
    for tree in TREES:
        paths.update(p for p in (ROOT / tree).rglob('*') if p.is_file())
    paths = sorted(p for p in paths if '__pycache__' not in p.parts
                   and p.suffix not in ('.pyc', '.log')
                   and p.name not in ('web_page.hpp', 'mame_test.py'))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(args.output, 'w:gz') as archive:
        for path in paths:
            if path.is_symlink():
                raise ValueError(f'Symlinks are not allowed in publication input: {path}')
            archive.add(path, arcname='faderOS/' + str(path.relative_to(ROOT)), recursive=False)
    print(f'Prepared {args.output}: {len(paths)} source files; review before publishing.')
    print('faderOS name confirmed; GNU GPLv3 source bundle prepared.')


if __name__ == '__main__':
    main()
