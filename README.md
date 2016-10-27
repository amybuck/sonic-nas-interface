sonic-nas-interface
===================

This repo contains the interface portion of the network abstraction service (NAS). This creates interfaces in the Linux kernel corresponding to the NPU front-panel ports, manages VLAN and LAG configurations, statistics management, and control packet handling.

Build
---------
Please see the instructions in the sonic-nas-manifest repo for more details on the common build tools. See [sonic-nas-manifest](https://github.com/Azure/sonic-nas-manifest) for complete information on common build tools.

### Build requirements
* `sonic-base-model`
* `sonic-common`
* `sonic-nas-common`
* `sonic-object-library`
* `sonic-logging`
* `sonic-nas-ndi`
* `sonic-nas-ndi-api`
* `sonic-nas-linux`

### Dependent Packages
* `libsonic-logging-dev` 
* `libsonic-logging1`
* `libsonic-model1` 
* `libsonic-model-dev`
* `libsonic-common1` 
* `libsonic-common-dev` 
* `libsonic-object-library1` 
* `libsonic-object-library-dev`
* `sonic-sai-api-dev` 
* `libsonic-nas-common1` 
* `libsonic-nas-common-dev` 
* `sonic-ndi-api-dev` 
* `libsonic-nas-platform` 
* `libsonic-nas-ndi1` 
* `libsonic-nas-ndi-dev` 
* `libsonic-nas-linux1` 
* `libsonic-nas-linux-dev` 
* `libsonic-sai-common1` 
* `libsonic-sai-common-utils1`

### Build command
    sonic_build  --dpkg libsonic-logging-dev libsonic-logging1 libsonic-model1 libsonic-model-dev libsonic-common1 libsonic-common-dev libsonic-object-library1 libsonic-object-library-dev sonic-sai-api-dev libsonic-nas-common1 libsonic-nas-common-dev sonic-ndi-api-dev  libsonic-nas-ndi1 libsonic-nas-ndi-dev libsonic-nas-linux1 libsonic-nas-linux-dev libsonic-nas-platform --apt libsonic-sai-common1 libsonic-sai-common-utils1 -- clean binary

(c) Dell 2016
