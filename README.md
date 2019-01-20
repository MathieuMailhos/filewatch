# Filewatch

Filewatch looks for modifications in the given files and executes the custom command. It uses the `inotify` Linux kernel API to trigger commands based on filesystem notifications (e.g. not POSIX).

## Synopsis

```
  Usage: filewatch [-m single|concurrent|override] files... command
```

## Options

`-m` _mode_ is defining the behavior of the program regarding launched commands.
It can be one of those three:

* `s|single`: one command is running at a time and is blocking
* `c|concurrent`: multiple commands can be running at the same time
* `o|override`: latest command takes over the existing one


## Description

Educational project around C Linux programming to play with _inotify_, _signalfd_/_sigaction_, _fork_/_exec_, _poll_ and other sys calls. I am not using _system_ for this purpose and hence, the command is not interpreted in a shell. Static code and memory analysis were performed with Valgrind and Clang scan-build but issues, PR and code review are completely welcome. If you like in this project, you may find [inotifywait(1)](https://linux.die.net/man/1/inotifywait) and [fswatch](https://github.com/emcrisostomo/fswatch) interesting.
