## NICOF - Non-Invasive COmmunication Facility

NICOF is a set of programs for **VM/370-R6** the mainframe operating system
running under the [Hercules](http://www.hercules-390.eu/) emulator giving virtual
machines the ability to actively communicate with the outside world of the emulated
mainframe.

Based on proxy VMs running inside VM/370 and a Java program running besides of
the Hercules emulator, it provides a set of APIs to user VMs running CMS (the
standard OS for users of VM/370):

- a BSD-like socket API (subset) supporting IPv4 stream and datagram sockets

- stdio-like APIs allowing to access files on the system hosting the Hercules emulator

- a low-level API for using the NICOF infrastructure for own communication protocols

Some programs built with these APIs are available:

- `CMSFTPD`, an FTP server running in an user VM under CMS, providing access to the
minidisks of this VM from outside the VM/370 resp. Hercules machine with standard FTP
clients.

- `NHFS`and `HACC`, CMS programs allowing to access files in the file system on
the outside of the VM/370 resp. the Hercules machine.

The files in this repository will probably be interesting only for users
of VM/370-R6 SixPack 1.2 (or older), although NICOF is known to work on VM/ESA 1.5
(with 370 feature). These users will however probably already know NICOF, as it was published
first in the files area of the Yahoo H390-VM discussion group, the main exchange
platform for VM/370 users.

This repository is an alternative location for the tools.

#### History and status

The first experiments for accessing low level assembler functionality from C programs
under CMS started in June 2012, targeting communication with an external Java program
which would provide services of the OS hosting Hercules.

The first public release of NICOF was for version 0.5.0, which provided the basic
communication infrastructure as well as the first usable tool `NHFS`.

The next release was version 0.6.0 featuring the socket API (stream sockets)
and the FTP server `CMSFTPD`.

Follow-up updates introduced small modifications to allow using NICOF with (slightly)
newer versions of VM/xxx than VM/370-R6, provided S/370-I/O is still supported.
These modifications became part of version 0.7.0. 

The last publication as version 0.7.0 is simply an update instead of a complete
installation package as the previous version. It is merely a delta to a working NICOF
0.6.0 installation in a VM/370 system. Version 0.7.0 mostly adds datagram socket support
for the socket API and the ability to give an IP address to VMs using the socket API. 

Development of NICOF ended with version 0.7.0.

#### License

The NICOF binaries and sources are released to the public domain.

#### Repository subdirectories 

The directories `cms` and `java` contain the sources for the last version 0.7.0
of NICOF:

- the directory [cms](./cms) contains the files to build the NICOF components for the mainframe
side, besides the `ASSEMBLE`, `H` and `C` files these are the `EXEC` scripts for building and running
the `MODULE` files as well as some support files.

- the directory [java](./java) contains the Eclipse project for the external Java program
of the NICOF communication infrastructure.

The directory [publications](./publications) has a subdirectory for each release of NICOF uploaded
to the files area of the Yahoo H390-VM discussion group.

All of the packages contain both the binaries as well as the source code for the CMS libraries
and programs resp. the external Java process. 

These are the following versions published at the specified date:

- [0.5.0](./publications/v0.5.0) : 2012-12-09

- [0.6.0](./publications/v0.6.0) : 2014-07-30

- [0.7.0](./publications/v0.7.0) : 2017-09-20

Each subdirectory contains the original ZIP-files published for the
corresponding release, as well as the documentation PDF file contained
in this ZIP-file.    
The files to be installed on the emulated mainframe are delivered as AWS
tape files, which can be mounted by the Hercules emulator. The tapes were
written with the CMS `TAPE` command in versions 0.5.0 and 0.6.0 and with
the CMS `VMFPLC2` command in version 0.7.0. 
