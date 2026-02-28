# COMP304 Project 1 - kyaylali22 - Kaya Yaylali — Shellish

A custom Unix shell called **shellish**

---

## Compilation

```bash
gcc -o shellish shellish-skeleton.c
```

## Running

```bash
./shellish
```

The shell displays a prompt in the format:
```
<user>@<hostname>:<cwd> shellish$
```

---

## Features

### Part I — Basic Shell

- **External command execution** via `fork()` + `execv()` with manual `PATH` resolution
- **Background process execution** using the `&` suffix (e.g., `sleep 10 &`)
- **Built-in `cd`** — change the current working directory
- **Built-in `exit`** — terminate the shell
- **Up-arrow key** — recall the previous command

**Example usage:**
```
shellish$ ls -la
shellish$ sleep 5 &
[bg] PID 1234 running in background
shellish$ cd /tmp
shellish$ exit
```

---

### Part II — I/O Redirection and Piping

#### I/O Redirection

| Syntax | Effect |
|--------|--------|
| `cmd < file` | Redirect stdin from file |
| `cmd > file` | Redirect stdout to file (overwrite) |
| `cmd >> file` | Redirect stdout to file (append) |

**Example:**
```
shellish$ ls > output.txt
shellish$ cat < output.txt
shellish$ echo "hello" >> output.txt
```

#### Piping

Chains multiple commands with `|`. Supports pipelines of arbitrary length (up to 17 commands).

**Example:**
```
shellish$ ls -la | grep shellish | wc -l
shellish$ cat /etc/passwd | cut -d: -f1 | sort
```

---

### Part III — Built-in Commands

#### (a) `cut` — Field Extractor

A built-in implementation of the Unix `cut` command. Reads from stdin, splits each line by a delimiter, and prints the specified fields.

**Syntax:**
```
cut -d<delimiter> -f<field1>,<field2>,...
```

**Examples:**
```
shellish$ echo "a:b:c:d" | cut -d: -f1,3
a:c

shellish$ cat /etc/passwd | cut -d: -f1,6
root:/root
...
```

- Default delimiter is TAB (`\t`)
- Fields are 1-based (first field is `-f1`)
- Supports combined flag syntax (e.g., `-d:` and `-f1,3`)

---

#### (b) `chatroom` — Multi-User Chat via Named Pipes

A real-time group chat system using POSIX named pipes (FIFOs).

**Syntax:**
```
chatroom <roomname> <username>
```

**How it works:**
- Creates a room directory at `/tmp/chatroom-<roomname>/`
- Creates a named pipe for the current user: `/tmp/chatroom-<roomname>/<username>`
- Forks a **reader** child to continuously print incoming messages
- The parent reads user input and broadcasts to all other users in the room
- Press **Ctrl+D** to exit the chatroom

**Example (two terminals):**

Terminal 1:
```
shellish$ chatroom myroom alice
Welcome to myroom!
[myroom] alice > Hello!
[myroom] alice: Hello!
[myroom] bob: Hi Alice!
```

Terminal 2:
```
shellish$ chatroom myroom bob
Welcome to myroom!
[myroom] alice: Hello!
[myroom] bob > Hi Alice!
[myroom] bob: Hi Alice!
```

---

#### (c) `calc` — Arithmetic Calculator

A simple built-in calculator supporting four arithmetic operations.

**Syntax:**
```
calc <num1> <operator> <num2>
```

**Supported operators:** `+`, `-`, `*`, `/`

**Examples:**
```
shellish$ calc 10 + 5
15

shellish$ calc 7 / 2
3.5

shellish$ calc 9 / 3
3

shellish$ calc 5 / 0
Error: division by zero
```

- Prints result as an integer if it is a whole number, otherwise as a decimal
- Reports an error for division by zero

---

## Implementation Notes

- Uses `execv()` (not `execvp()`) with manual `PATH` traversal via `strtok`
- I/O redirection uses `open()` + `dup2()` to replace file descriptors before `execv`
- Piping uses `pipe()` system call; N-1 pipes created for N piped commands
- Chatroom uses `mkfifo()` for named pipes and `O_RDWR`/`O_NONBLOCK` to avoid blocking
- Terminal input is handled in raw mode using `termios` for character-by-character reading

---

## File Structure

```
COMP304-PROJECT-1/
├── shellish-skeleton.c   # Full implementation
├── README.md             # This file
└── imgs/                 # Screenshots for submission
```
---

## Repository

[https://github.com/Ayyaki/COMP304-Project-1-KayaYaylali](https://github.com/Ayyaki/COMP304-Project-1-KayaYaylali)
 
---
## AI Assistance Disclosure

This project used Claude (Anthropic) AI model for the following:
- Writing this README file.
- Identifying which standard library headers to include.
- Understanding the `dup2()` system call for I/O redirection.
- Determining test case examples for each part.
