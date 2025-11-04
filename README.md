# 🐚 My Custom Shell in C

This is a simple Unix-like shell built in C.  
It supports basic command execution, I/O redirection, single piping, background jobs, environment variable handling, and a few built-in commands.  
The goal of this project was to understand how a shell actually works behind the scenes — using system calls, process creation, and I/O management.

---

## Features

- **Execute external commands** from your system’s PATH
- **Built-in commands**:  
  `cd`, `pwd`, `echo`, `history`, `env`, `set`, `unset`, `jobs`, `kill`, `help`, and `exit`
- **I/O Redirection**:
  - Input: `<`
  - Output overwrite: `>`
  - Output append: `>>`
- **Piping**: Supports a single `|` between two commands
- **Background processes**: Run commands with `&` and track them with `jobs`
- **Job control**:  
  View active jobs or kill a process using its PID
- **Command history**: Keeps a list of previous commands during the session
- **Environment variables**:  
  Set, unset, and print environment variables
- **Signal handling**: Gracefully handles Ctrl+C and reaps background processes

---

## How to Build

You’ll need:
- A C compiler (like `gcc`)
- A Unix-like OS (Linux, macOS, or WSL)

To compile:

```bash
gcc -o myshell updated_shell.c


To run:
./myshell
