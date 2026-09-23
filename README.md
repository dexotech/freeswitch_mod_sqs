# mod_sqs

This is a FreeSWITCH module, providing functionality to send events and CDRs to AWS' Simple Queue Service (SQS)

---

## 🚀 Features
- Internal queue with circuit breaker mechanism
- Supports Events and CDRs via different profile types
- Supports parallel submissions
- Supports standard and fifo queues

---

## Requiremets
- curl develompent package (curl-devel, libcurl4-openssl-dev, etc.)
- freeswitch development package (freeswitch-devel, libfreeswitch-dev, etc.)

---

## 📦 Build

```bash
# Clone the repository
git clone --recurse-submodules https://github.com/dexotech/freeswitch_mod_sqs.git
cd freeswitch_mod_sqs

# Build dependencies
cd deps/aws-sdk-cpp
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=../../../ -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=off -DAUTORUN_UNIT_TESTS=off -DBUILD_ONLY="sqs"
cmake --build .
cmake --install .
cd ../../..

# Build
export PKG_CONFIG_PATH=lib64/pkgconfig/
autoreconf -fi
./configure
make
```

Compiled mod_sqs.so will be in .libs directory after successful compilation.

---

## License
This code is licensed under the GNU General Public Licenseversion 3 or later (GPL-3.0-or-later).
See [LICENSE](LICENSE) for the full license text.
