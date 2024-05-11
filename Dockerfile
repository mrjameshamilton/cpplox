FROM ubuntu:22.04

RUN apt-get update -y
RUN apt-get upgrade -y
RUN apt-get install -y wget software-properties-common gnupg ca-certificates gpg \
    cmake ninja-build build-essential lsb-release zlib1g-dev libzstd-dev
RUN add-apt-repository ppa:ubuntu-toolchain-r/test
RUN apt install gcc-13 g++-13 -y
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 13 --slave /usr/bin/g++ g++ /usr/bin/g++-13
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null
RUN apt-add-repository "deb https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main"
RUN apt-get update -y
RUN apt-get install kitware-archive-keyring cmake -y
RUN apt-get install git -y

RUN wget https://apt.llvm.org/llvm.sh
RUN chmod +x llvm.sh
RUN ./llvm.sh 18
RUN ln -s /bin/lli-18 /bin/lli
RUN ln -s /bin/clang-18 /bin/clang

# Get craftinginterpreters repo for the test suite
ENV CRAFTING_INTERPRETERS_PATH=/craftinginterpreters
RUN git clone --depth 1 https://github.com/munificent/craftinginterpreters.git $CRAFTING_INTERPRETERS_PATH
RUN wget https://storage.googleapis.com/dart-archive/channels/stable/release/2.19.6/linux_packages/dart_2.19.6-1_amd64.deb
RUN dpkg -i dart_2.19.6-1_amd64.deb
RUN cd $CRAFTING_INTERPRETERS_PATH && make get
RUN cd /

WORKDIR /app

CMD rm -rf build && cmake -S . -B build -G Ninja .. && ninja -C build && ninja -C build test
