# Third-party software and distribution notices

The repository's own code is licensed under the [MIT License](LICENSE). That
license does **not** replace the licenses of third-party components. The
versions below reflect the checked-in submodules and the current Linux build;
recheck this inventory whenever dependencies or the distribution format change.

## C++ server

| Component | How used | License and notice |
| --- | --- | --- |
| [uWebSockets](https://github.com/uNetworking/uWebSockets) | Header code compiled into the server | Apache-2.0; [license text](cpp/third_party/uWebSockets/LICENSE). Copyright Alex Hultman and contributors; retain notices in source files. |
| [uSockets](https://github.com/uNetworking/uSockets) | Static library compiled into the server | Apache-2.0; [license text](cpp/third_party/uWebSockets/uSockets/LICENSE). Copyright Alex Hultman and contributors; retain notices in source files. |
| [jwt-cpp](https://github.com/Thalhammer/jwt-cpp) | Header code compiled into the server | MIT; [license text](cpp/third_party/jwt-cpp/LICENSE). Copyright (c) 2018 Dominik Thalhammer. |
| [nlohmann/json](https://github.com/nlohmann/json) | System-installed header code compiled into the server | MIT; copyright (c) 2013–2023 Niels Lohmann. See MIT notice below. |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | System shared library and headers | MIT; copyright (c) 2012 and 2023 Yuji Hirose. See MIT notice below. |
| [FreeTDS](https://www.freetds.org/) (`libsybdb`) | System shared library | LGPL-2.0-or-later for the linked library; see the installed package's copyright/license files and the [FreeTDS source](https://www.freetds.org/software.html). |
| [OpenSSL](https://www.openssl.org/source/license.html) | System shared libraries | License depends on installed version; the current Linux OpenSSL 3 package is Apache-2.0. Preserve its package notices if redistributing the libraries. |
| [zlib](https://zlib.net/) | System shared library | zlib License. Preserve the installed package's copyright notice if redistributing the library. |

The CMake `install` target ships this document, this project's license, and
the complete uWebSockets, uSockets, and jwt-cpp license texts with the C++
executable. It does **not** copy operating-system shared libraries. If a release
bundles those libraries, include their exact version's license and copyright
files as well; for FreeTDS, also satisfy applicable LGPL source/relinking
requirements. The unused uWebSockets submodules (such as BoringSSL and lsquic)
are not linked by the current CMake configuration.

### MIT notice for components without a separately bundled license file

Copyright (c) 2013–2023 Niels Lohmann (nlohmann/json)

Copyright (c) 2012 Yuji Hirose (cpp-httplib package license)

Copyright (c) 2023 Yuji Hirose (installed httplib.h)

Copyright (c) Microsoft Corporation (Microsoft JDBC Driver for SQL Server)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Spring server

The executable Spring Boot JAR embeds third-party JARs under `BOOT-INF/lib/`.
The complete resolved component/version list is maintained in
[`spring/gradle.lockfile`](spring/gradle.lockfile); it includes transitive
libraries, not just those named in `spring/build.gradle`. Important direct
components include Spring Boot/Spring Framework (Apache-2.0), JJWT
(Apache-2.0), Jackson (Apache-2.0), and the Microsoft JDBC Driver (MIT).
Several components, especially Jakarta/JSTL and transitive dependencies, have
their own notices or license choices. The lockfile is an inventory, **not** a
license grant or a substitute for those notices.

H2 (EPL-1.0 or MPL-2.0) is a test dependency and is not embedded in the
production boot JAR. The web pages request Inter, JetBrains Mono, and Outfit
fonts from Google Fonts at runtime; font files are not checked into this
repository or bundled in the boot JAR. Pretendard and Fira Code are not used
by the current web assets.

This document, the project's LICENSE, and a complete Apache-2.0 license text
are embedded in the boot JAR under `META-INF/agora-licenses/`. The upstream
license/NOTICE files inside each `BOOT-INF/lib/*.jar`
remain part of those nested JARs. Before distributing a new binary release,
review the resolved runtime dependency set and preserve all applicable
upstream license and NOTICE files, including those absent from individual JARs.
Do not assume that a different dependency version has the same terms.

The Docker Compose services download separate vendor images. This repository
does not redistribute those images; distributing an image requires reviewing
that image's own license and included notices separately.
