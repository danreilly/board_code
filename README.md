# Board Code

This code runs on the ARM in the PS portion of the FPGA.


## Installation

For development, it's probably best to clone this repo to a shared directory on a host (Dan uses his PC, but you could use a linux host too) instead of onto the ZCU boards.  Then all the boards can mount that share, so they always all use the same sources and executables.  For example:

`modprobe cifs`
`mount.cifs //10.0.0.32/share /mnt/s -o cred=~/.smbcred`



## u

This is the main utility program. run
`u init`
to initialize the qnic


## tst

This transmits headers out the DAC, and captures the signal on the ADC, and stores it in the out directory.  This (or something like it) was the main workhorse during the happycamper event.  There are a few command line options, and to see them run "ech ?".  See also the qnicll repository.



## qregd

This is the qreg demon (which listens on port 5000).  For now, you have to manually start it.  This must be running on the zcu106 in order for the the qnicll library to work.



## cfg

This directory contains initialation files.
`ini_all.txt` - init params for all qnics
`ini_<name>.txt` - init params for a specific qnic

Per-unit calibration files may be copied into this directory.  Typically you then set a string in the `ini_<name>.txt` file to point to it.


## source files

src/tst.c   - source code for tst

src/qregs.c - register access routines

src/ini.c   - manages variables in a separate namespace,
              can read & write them from/to a text file.
	      similar in concept to JSON or numerous other serialized formats.
	      This uses matlab syntax.
