# Kernel Build Manual

## Prerequisites
It is up to developers to prepare the host machine; it requires:

* [Setup Cross Compiler](https://github.com/compulab-yokneam/meta-bsp-imx8mp/blob/kirkstone/Documentation/toolchain.md#linaro-toolchain-how-to)

## CompuLab Linux Kernel setup

* WorkDir:
```
mkdir -p compulab-bsp/linux-compulab && cd compulab-bsp/linux-compulab
```

* Set a CompuLab machine:

| Machine | Command Line |
|---|---|
|ucm-imx8m-plus|```export MACHINE=ucm-imx8m-plus```|
|som-imx8m-plus|```export MACHINE=som-imx8m-plus```|
|iot-gate-imx8plus|```export MACHINE=iot-gate-imx8plus```|
|ucm-imx93|```export MACHINE=ucm-imx93```|

* Clone the source code:
```
git clone -b linux-compulab_v5.15.71 https://github.com/compulab-yokneam/linux-compulab.git .
```

## Compile the Kernel

* Apply the default CompuLab config:
```
make ${MACHINE}_defconfig compulab.config
```

* Ussue menuconfig on order to change the default CompuLab configuration:
```
make menuconfig
```

* Build the kernel
```
nice make -j`nproc`
```

* Builds' targets
  
| target | make command |
|---|---|
|Uncompressed kernel image|```make -j`nproc` Image```|
|Build all modules|```make -j`nproc` modules```|
|Build device tree blobs|```make -j`nproc` dtbs```|

* [Deploy the CompuLab Linux Kernel to CompuLab devices](https://github.com/compulab-yokneam/Documentation/blob/master/etc/linux_kernel_deployment.md#create-deb-package)



## ES
To Cross Compile:

```bash
export MACHINE=som-imx8m-plus

export ARCH=arm64
export CROSS_COMPILE=/opt/gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-

${CROSS_COMPILE}gcc --version



```


### Apply the default CompuLab config:

```bash
make ${MACHINE}_defconfig compulab.config
```

### Ussue menuconfig on order to change the default CompuLab configuration:

```bash
make menuconfig
```

### Build the kernel:

```bash
nice make -j`nproc`
```

### Builds' targets:

| target | make command |
| ------------------------- | ------------------------- |
| Uncompressed kernel image | make -j`nproc` Image |
| Build all modules | make -j`nproc` modules |
| Build device tree blobs | make -j`nproc` dtbs |

