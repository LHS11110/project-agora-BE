# Third-Party Software Licenses and Notices

This project (`Project Agora Backend`) utilizes the following open-source third-party libraries and components. This document provides attribution and lists the licenses governing their usage in compliance with their respective licensing terms.

---

## Summary of Third-Party Components

| Component | Category | License | Copyright / Author |
| :--- | :--- | :--- | :--- |
| **uWebSockets** | C++ Real-time Server | Apache License 2.0 | Copyright (c) 2018-2023 Alex Hultman |
| **uSockets** | C++ Network Layer | Apache License 2.0 | Copyright (c) 2018-2023 Alex Hultman |
| **cpp-httplib** | C++ HTTP Client & Server | MIT License | Copyright (c) 2024 Yuji Hirose |
| **nlohmann/json** | C++ JSON Processing | MIT License | Copyright (c) 2013-2022 Niels Lohmann |
| **FreeTDS (sybdb)** | C++ MSSQL Client (Dynamic Linking) | GNU LGPL v2.1+ | Copyright (c) 1998-2024 Brian Bruns & FreeTDS Contributors |
| **zlib** | C++ Compression | zlib License | Copyright (c) 1995-2024 Jean-loup Gailly and Mark Adler |
| **Spring Boot & Framework** | Java Web Framework | Apache License 2.0 | Copyright (c) 2002-2026 VMware, Inc. / Broadcom |
| **Microsoft JDBC Driver for SQL Server** | Java MSSQL Driver | MIT License | Copyright (c) Microsoft Corporation |
| **JJWT (io.jsonwebtoken)** | Java JWT Library | Apache License 2.0 | Copyright (c) 2014-2024 Les Hazlewood & Contributors |
| **Jackson Databind** | Java JSON Serialization | Apache License 2.0 | Copyright (c) 2009-2024 FasterXML, LLC |
| **Apache Tomcat Embed** | Java Servlet / JSP Container | Apache License 2.0 | Copyright (c) 1999-2024 The Apache Software Foundation |
| **Jakarta Servlet / JSTL API** | Java Web Standard | EPL 2.0 / GPL v2 w/ CPE | Copyright (c) Eclipse Foundation |
| **GlassFish JSTL Implementation** | Java JSTL Engine | EPL 2.0 / GPL v2 w/ CPE | Copyright (c) Eclipse Foundation |
| **H2 Database** | In-Memory Testing Database | EPL 1.0 / MPL 2.0 | Copyright (c) 2004-2024 Thomas Mueller |
| **Pretendard Font** | Web Font | SIL Open Font License 1.1 | Copyright (c) 2021 Kil Hyung-jin |
| **Inter Font** | Web Font | SIL Open Font License 1.1 | Copyright (c) 2016-2020 Rasmus Andersson |
| **JetBrains Mono Font** | Web Font | SIL Open Font License 1.1 | Copyright (c) 2020 JetBrains s.r.o. |
| **Fira Code Font** | Web Font | SIL Open Font License 1.1 | Copyright (c) 2014-2020 Nikita Prokopov |

---

## Detailed License Texts and Notices

### 1. Apache License, Version 2.0
Applicable to: **uWebSockets**, **uSockets**, **Spring Boot & Framework**, **JJWT**, **Jackson Databind**, **Apache Tomcat Embed**

```
                                 Apache License
                           Version 2.0, January 2004
                        http://www.apache.org/licenses/

   TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION

   1. Definitions.
      "License" shall mean the terms and conditions for use, reproduction,
      and distribution as defined by Sections 1 through 9 of this document.

      "Licensor" shall mean the copyright owner or entity authorized by
      the copyright owner that is granting the License.

      "Legal Entity" shall mean the union of the acting entity and all
      other entities that control, are controlled by, or are under common
      control with that entity. For the purposes of this definition,
      "control" means (i) the power, direct or indirect, to cause the
      direction or management of such entity, whether by contract or
      otherwise, or (ii) ownership of fifty percent (50%) or more of the
      outstanding shares, or (iii) beneficial ownership of such entity.

      "You" (or "Your") shall mean an individual or Legal Entity
      exercising permissions granted by this License.

      "Source" form shall mean the preferred form for making modifications,
      including but not limited to software source code, documentation
      source, and configuration files.

      "Object" form shall mean any form resulting from mechanical
      transformation or translation of a Source form, including but
      not limited to compiled object code, generated documentation,
      and conversions to other media types.

      "Work" shall mean the work of authorship, whether in Source or
      Object form, made available under the License, as indicated by a
      copyright notice that is included in or attached to the work.

      "Derivative Works" shall mean any work, whether in Source or Object
      form, that is based on (or derived from) the Work and for which the
      editorial revisions, annotations, elaborations, or other modifications
      represent, as a whole, an original work of authorship.

      "Contribution" shall mean any work of authorship, including
      the original version of the Work and any modifications or additions
      to that Work or Derivative Works thereof, that is intentionally
      submitted to Licensor for inclusion in the Work by the copyright owner
      or by an individual or Legal Entity authorized to submit on behalf of
      the copyright owner.

   2. Grant of Copyright License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      copyright license to reproduce, prepare Derivative Works of,
      publicly display, publicly perform, sublicense, and distribute the
      Work and such Derivative Works in Source or Object form.

   3. Grant of Patent License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      (except as stated in this section) patent license to make, have made,
      use, offer to sell, sell, import, and otherwise transfer the Work.

   4. Redistribution. You may reproduce and distribute copies of the
      Work or Derivative Works thereof in any medium, with or without
      modifications, and in Source or Object form, provided that You
      meet the following conditions:

      (a) You must give any other recipients of the Work or
          Derivative Works a copy of this License; and

      (b) You must cause any modified files to carry prominent notices
          stating that You changed the files; and

      (c) You must retain, in the Source form of any Derivative Works
          that You distribute, all copyright, patent, trademark, and
          attribution notices from the Source form of the Work,
          excluding those notices that do not pertain to any part of
          the Derivative Works; and

      (d) If the Work includes a "NOTICE" text file as part of its
          distribution, then any Derivative Works that You distribute must
          include a readable copy of the attribution notices contained
          within such NOTICE file.

   5. Submission of Contributions. Unless You explicitly state otherwise,
      any Contribution intentionally submitted for inclusion in the Work
      by You to the Licensor shall be under the terms and conditions of
      this License, without any additional terms or conditions.

   6. Trademarks. This License does not grant permission to use the trade
      names, trademarks, service marks, or product names of the Licensor,
      except as required for reasonable and customary use in describing the
      origin of the Work and reproducing the content of the NOTICE file.

   7. Disclaimer of Warranty. Unless required by applicable law or
      agreed to in writing, Licensor provides the Work (and each
      Contributor provides its Contributions) on an "AS IS" BASIS,
      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
      implied, including, without limitation, any warranties or conditions
      of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
      PARTICULAR PURPOSE.

   8. Limitation of Liability. In no event and under no legal theory,
      whether in tort (including negligence), contract, or otherwise,
      unless required by applicable law or agreed to in writing, shall
      any Contributor be liable to You for damages, including any direct,
      indirect, special, incidental, or consequential damages of any
      character arising as a result of this License or out of the use or
      inability to use the Work.

   9. Accepting Warranty or Additional Liability. While redistributing
      the Work or Derivative Works thereof, You may choose to offer,
      and charge a fee for, acceptance of support, warranty, indemnity,
      or other liability obligations and/or rights consistent with this
      License.
```

---

### 2. MIT License
Applicable to: **cpp-httplib**, **nlohmann/json**, **Microsoft JDBC Driver for SQL Server**

```
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
```

- **cpp-httplib Copyright**: Copyright (c) 2024 Yuji Hirose
- **nlohmann/json Copyright**: Copyright (c) 2013-2022 Niels Lohmann
- **mssql-jdbc Copyright**: Copyright (c) Microsoft Corporation

---

### 3. GNU Lesser General Public License (LGPL), Version 2.1
Applicable to: **FreeTDS (sybdb)**

```
FreeTDS is free software; you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation;
either version 2 of the License, or (at your option) any later version.

This project dynamically links against the shared library `libsybdb.so` provided by FreeTDS.
FreeTDS source code is freely available at https://www.freetds.org/
```

---

### 4. zlib License
Applicable to: **zlib**

```
Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

---

### 5. Eclipse Public License - v 2.0 (EPL-2.0)
Applicable to: **Jakarta Servlet & JSTL API / GlassFish JSTL Implementation**

```
The Eclipse Public License 2.0 is available at https://www.eclipse.org/legal/epl-2.0/
Source code for Jakarta EE components is available at https://github.com/jakartaee
```

---

### 6. SIL Open Font License 1.1
Applicable to: **Pretendard, Inter, JetBrains Mono, Fira Code**

```
SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007
http://scripts.sil.org/OFL

The fonts are licensed under the SIL Open Font License, Version 1.1.
This license allows the fonts to be used, studied, modified and redistributed freely.
```
