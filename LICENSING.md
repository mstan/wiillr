# Licensing

Copyright (c) 2026 Matthew Stanley for original project code.

Original wiillr code is licensed under **GNU GPL version 3 or later**
(GPL-3.0-or-later), except for the independent annotation utility listed below.
The full GPL text is in [LICENSE](https://github.com/mstan/wiillr/blob/main/LICENSE). This covers the native runtime,
interfaces, code generators, build and packaging scripts, and their tests.

## Independent annotation utility

[tools/annotate_wii_menu.py](https://github.com/mstan/wiillr/blob/main/tools/annotate_wii_menu.py) and its isolated test,
[tests/test_wii_menu_annotations.py](https://github.com/mstan/wiillr/blob/main/tests/test_wii_menu_annotations.py), use
**PolyForm Noncommercial 1.0.0**. The exact terms are in
[LICENSES/PolyForm-Noncommercial-1.0.0.txt](https://github.com/mstan/wiillr/blob/main/LICENSES/PolyForm-Noncommercial-1.0.0.txt).
Both files carry their own SPDX identifier and required copyright notice.

This standalone source-inspection utility uses Python and an optional Capstone
decoder. It is not linked into the runtime or used to generate, build, install
or run the native application. The noncommercial terms apply to these two files
only. They do not restrict the GPL runtime or its corresponding source, and
they do not change the ownership or licenses of inspected inputs or metadata.

## Third-party components and distribution

Files with upstream notices, third_party/, submodules and external inputs retain
their own licenses. Dolphin-derived code retains GPL-2.0-or-later notices, and
the GPLv3 shared machine/compiler/graphics components retain their applicable
terms. Upstream MIT, BSD, Apache and other notices remain in place. The original
host-binding files in the Dolphin ports use GPL-3.0-or-later; their upstream
implementations keep their original notices.

The linked native application is distributed under the applicable GPL terms.
Provide its corresponding source, build instructions and required notices;
copying a license alone does not satisfy the source-distribution requirements.
Packaged current-project notices use wiillr-GPL3.txt and wiillr-LICENSING.md in
LICENSES/. Imported component notices remain separate.

Earlier commits and release archives retain the licenses supplied with those
versions. This prospective policy does not revoke earlier license grants.
