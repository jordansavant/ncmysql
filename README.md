# ncmysql

An ncurses explorer and client for MySQL databases written in C

<img src="https://raw.githubusercontent.com/jordansavant/ncmysql/master/tui.png" />

## usage

```
Usage:
  ./bin/main.out -i help
  ./bin/main.out -h mysql-host [-l port=3306] -u mysql-user [-p mysql-pass] [-s ssh-tunnel-host]
  ./bin/main.out -f connection-file=connections.csv [-d delimeter=,]
```

The binary runs in two modes: one to directly connect to a host similar to the mysql cli client, and the second reads from a connections csv file.

Running with the `-i` flag will print the usage.

Running with the host flags `-hlups` requires host and user minimally. If a password is not supplied a prompt will accept a password.
Optionally an SSH host can be provided such as `user@host` and the program will fork a system call to establish the SSH tunnel in port ranges of 2200-2216 and use this tunnel to establish the MySQL connection.

Running with file flags `-fd` will read a CSV file of connections in the format: `name,host,port,user,pass,tunnel`. The `-d` option allows you to define the csv delimeter instead of a default comma `,`.
If no options are presented it will look within the executable directory for a `connections.csv` file.

A debug log is placed in `logs/log` which you can view for run time information.

## building

I have built this on Ubuntu and macOS High Sierra using Homebrew.

Dependencies:

- ncurses
- ncurses menu
- mysqlclient c api library [](https://dev.mysql.com/doc/c-api/8.0/en/c-api-introduction.html)

Building on Linux (Ubuntu):

- `apt-get install libncurses5-dev libmysqlclient-dev`
- compile only: `make` will place executable as `bin/main.out`
- compile and run: `make run` or with args as `make ARGS="[args here]" run`

Building on Homebrew (macOS High Sierra):

- `brew install ncurses mysql`
- compile only: `make macos` will place executable as `bin/main.out`
- compile and run: `make macos-run` or with args as `make ARGS="[args here]" macos-run`

## todo

- rethink "die" statements in sqlops, seems like nonsense, consumer should define error handling
- cell editor needs to support multiple primary keys
- text editor needs quality of life work for various keys like HOME, END, delete a line, insert a newline etc
- query history you can cycle through
- error popup on macos has wbkgd values with ????? failed character renders, need to manually render backgrounds
- cell editor has weird render issues for row past end since i left a buffer line
- creating a table does not refresh the table list and show it
- cell editor not applying updates to content with inserted new lines
