# mod_sqs

This is a FreeSWITCH module, providing functionality to send events and CDRs to AWS' Simple Queue Service (SQS)

---

## 🚀 Features
- Internal queue with circuit breaker mechanism
- Supports Events and CDRs via different profile types
- Supports parallel submissions
- Supports standard and fifo queues

---

## Dependencies
- curl development package (curl-devel, libcurl4-openssl-dev, etc.)
- FreeSWITCH development package (freeswitch-devel, libfreeswitch-dev, etc.)
- AWS SDK for C++ (aws-cpp-sdk-core and aws-cpp-sdk-sqs), either built from the bundled `deps/aws-sdk-cpp` submodule or installed from your distribution's packages (aws-cpp-sdk-core-devel, aws-cpp-sdk-sqs-devel, etc.)

---

## 📦 Build

The build system locates the AWS SDK and FreeSWITCH through pkg-config, so either build path below works.

### Option 1: Build the AWS SDK from the bundled submodule

```bash
# Clone the repository (with the AWS SDK submodule)
git clone --recurse-submodules https://github.com/dexotech/freeswitch_mod_sqs.git
cd freeswitch_mod_sqs

# Build the AWS SDK (SQS only) into the repository root
cd deps/aws-sdk-cpp
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=../../../ -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=off -DAUTORUN_UNIT_TESTS=off -DBUILD_ONLY="sqs"
cmake --build .
cmake --install .
cd ../../..

# Build the module against the SDK installed above.
# Point PKG_CONFIG_PATH at the directory the SDK .pc files were installed
export PKG_CONFIG_PATH=$PWD/lib/pkgconfig:lib64/pkgconfig/:$PKG_CONFIG_PATH
autoreconf -fi
./configure
make
```

### Option 2: Use distribution packages for the AWS SDK

```bash
# Install the required development packages from your distribution, e.g.:
#   RHEL/Fedora: dnf install aws-cpp-sdk-core-devel aws-cpp-sdk-sqs-devel freeswitch-devel
#   Debian/Ubuntu: apt install libcurl4-openssl-dev libfreeswitch-dev
# (AWS SDK packages are not available on every distribution; in that case use Option 1 to build from the submodule.)
git clone https://github.com/dexotech/freeswitch_mod_sqs.git
cd freeswitch_mod_sqs
autoreconf -fi
./configure
make
```

`./configure` finds the AWS SDK (aws-cpp-sdk-core, aws-cpp-sdk-sqs) and FreeSWITCH via pkg-config, so any installation location pkg-config can see works.
Set `PKG_CONFIG_PATH` if the `.pc` files live in a non-standard directory.

Useful configure options:
- `--with-modver=VER` — module version
- `--disable-assertions` — build without the assert function

Compiled mod_sqs.so will be in .libs directory after successful compilation.

---

## AWS Credentials

The `access_key` and `secret_key` parameters in `config/sqs.conf.xml` are
optional. When both are set, they are used directly (explicit XML
credentials take precedence). When either is absent or empty, the AWS SDK
default credential provider chain is used instead, in this order:

- environment variables (`AWS_ACCESS_KEY_ID` / `AWS_SECRET_ACCESS_KEY`)
- shared credential file (`~/.aws/credentials`)
- IAM role (instance profile on EC2, container credentials, etc.)

---

## License
This code is licensed under the GNU General Public License version 3 or later (GPL-3.0-or-later).
See [LICENSE](LICENSE) for the full license text.
