#include <dirent.h>  // opendir, readdir, closedir
#include <errno.h>
#include <fcntl.h>   // open, O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC, O_APPEND
#include <signal.h>  // kill, SIGTERM
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> // mkdir, mkfifo
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
const char *sysname = "shellish";

enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t {
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0) {
      struct command_t *c =
          (struct command_t *)malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = (char *)malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  // increase args size by 2
  command->args = (char **)realloc(command->args,
                                   sizeof(char *) * (command->arg_count += 2));

  // shift everything forward by 1
  for (int i = command->arg_count - 2; i > 0; --i)
    command->args[i] = command->args[i - 1];

  // set args[0] as a copy of name
  command->args[0] = strdup(command->name);
  // set args[arg_count-1] (last) to NULL
  command->args[command->arg_count - 1] = NULL;

  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}

/**
 * Part III (b): Built-in chatroom command.
 * Implements a simple group chat using named pipes (FIFOs).
 * Room directory: /tmp/chatroom-<roomname>/
 * Each user has a named pipe: /tmp/chatroom-<roomname>/<username>
 * Usage: chatroom <roomname> <username>
 */
void run_chatroom(struct command_t *command) {
  // Need at least: args[0]=chatroom, args[1]=roomname, args[2]=username, args[3]=NULL
  if (command->arg_count < 4) {
    printf("Usage: chatroom <roomname> <username>\n");
    exit(1);
  }

  char *roomname = command->args[1];
  char *username = command->args[2];

  // Create room directory if it does not exist
  char room_path[512];
  snprintf(room_path, sizeof(room_path), "/tmp/chatroom-%s", roomname);
  mkdir(room_path, 0777); // ignore error if already exists

  // Create user's named pipe if it does not exist
  char user_pipe[1024];
  snprintf(user_pipe, sizeof(user_pipe), "%s/%s", room_path, username);
  mkfifo(user_pipe, 0666); // ignore error if already exists

  printf("Welcome to %s!\n", roomname);
  fflush(stdout);

  // Fork a reader child: continuously reads from our named pipe and prints messages
  pid_t reader_pid = fork();
  if (reader_pid == 0) { // reader child
    // Open pipe with O_RDWR to avoid blocking (no need to wait for a writer)
    int fd = open(user_pipe, O_RDWR);
    if (fd < 0) { perror("chatroom: open pipe"); exit(1); }

    char buf[1024];
    ssize_t n;
    while (1) {
      n = read(fd, buf, sizeof(buf) - 1);
      if (n > 0) {
        buf[n] = '\0';
        if (n > 1 && buf[n - 1] == '\n') buf[n - 1] = '\0'; // strip newline
        // Print received message, then reprint the input prompt
        printf("\r[%s] %s\n[%s] %s > ", roomname, buf, roomname, username);
        fflush(stdout);
      }
    }
    close(fd);
    exit(0);
  }

  // Parent: show prompt, read user input, and broadcast to other users
  char input[1024];
  while (1) {
    printf("[%s] %s > ", roomname, username);
    fflush(stdout);

    if (fgets(input, sizeof(input), stdin) == NULL) break; // Ctrl+D exits

    // Trim trailing newline
    int len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') input[--len] = '\0';
    if (len == 0) continue; // ignore empty lines

    // Format message as "username: message"
    char message[2048];
    snprintf(message, sizeof(message), "%s: %s", username, input);

    // Echo our own message locally
    printf("[%s] %s\n", roomname, message);
    fflush(stdout);

    // Broadcast to all other users' pipes in the room
    DIR *dir = opendir(room_path);
    if (dir) {
      struct dirent *entry;
      while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. and our own pipe
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, username) == 0)
          continue;

        char target_pipe[1024];
        snprintf(target_pipe, sizeof(target_pipe), "%s/%s", room_path, entry->d_name);

        // Fork a writer child for each target user
        pid_t writer_pid = fork();
        if (writer_pid == 0) { // writer child
          // O_NONBLOCK: don't hang if the target user has no reader open
          int wfd = open(target_pipe, O_WRONLY | O_NONBLOCK);
          if (wfd >= 0) {
            char msg_nl[2050];
            snprintf(msg_nl, sizeof(msg_nl), "%s\n", message);
            write(wfd, msg_nl, strlen(msg_nl));
            close(wfd);
          }
          exit(0);
        }
        waitpid(writer_pid, NULL, 0); // wait for each writer to finish
      }
      closedir(dir);
    }
  }

  // Ctrl+D pressed — clean up reader child and exit
  kill(reader_pid, SIGTERM);
  waitpid(reader_pid, NULL, 0);
  exit(0);
}

/**
 * Part III (a): Built-in cut command.
 * Reads lines from stdin, splits by delimiter, prints specified fields.
 * Called from exec_single() in a forked child, so redirections are already set up.
 * @param command  the parsed command (args contain -d and -f flags)
 */
void run_cut(struct command_t *command) {
  char delimiter = '\t'; // default delimiter is TAB
  int fields[256];
  int field_count = 0;

  // Parse arguments: args[0]=name, args[1..arg_count-2]=flags, args[arg_count-1]=NULL
  for (int i = 1; i < command->arg_count - 1 && command->args[i]; i++) {
    char *arg = command->args[i];

    // -d or --delimiter: next arg (or concatenated) is the delimiter character
    if (strcmp(arg, "-d") == 0 || strcmp(arg, "--delimiter") == 0) {
      if (i + 1 < command->arg_count - 1 && command->args[i + 1])
        delimiter = command->args[++i][0];
    } else if (strncmp(arg, "-d", 2) == 0 && strlen(arg) > 2) {
      delimiter = arg[2]; // e.g. -d:
    }
    // -f or --fields: parse comma-separated list of 1-based field indices
    else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--fields") == 0) {
      if (i + 1 < command->arg_count - 1 && command->args[i + 1]) {
        char temp[256];
        strncpy(temp, command->args[++i], sizeof(temp) - 1);
        char *token = strtok(temp, ",");
        while (token && field_count < 256) {
          fields[field_count++] = atoi(token);
          token = strtok(NULL, ",");
        }
      }
    } else if (strncmp(arg, "-f", 2) == 0 && strlen(arg) > 2) {
      // e.g. -f1,6 (no space between flag and value)
      char temp[256];
      strncpy(temp, arg + 2, sizeof(temp) - 1);
      char *token = strtok(temp, ",");
      while (token && field_count < 256) {
        fields[field_count++] = atoi(token);
        token = strtok(NULL, ",");
      }
    }
  }

  // Read stdin line by line and print requested fields
  char line[4096];
  while (fgets(line, sizeof(line), stdin)) {
    // Strip trailing newline
    int len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
      line[--len] = '\0';

    // Split line by delimiter: collect pointers to each field in-place
    char *parts[256];
    int part_count = 0;
    parts[part_count++] = line;
    for (int i = 0; i < len && part_count < 256; i++) {
      if (line[i] == delimiter) {
        line[i] = '\0';                    // terminate current field
        parts[part_count++] = line + i + 1; // start of next field
      }
    }

    // Print requested fields in the order specified by -f
    for (int i = 0; i < field_count; i++) {
      int idx = fields[i] - 1; // convert 1-based index to 0-based
      if (i > 0)
        putchar(delimiter); // separate fields with the same delimiter
      if (idx >= 0 && idx < part_count)
        printf("%s", parts[idx]);
    }
    putchar('\n');
  }
}

/**
 * Part II: Execute a single command in the child process.
 * Handles I/O redirection with dup2, then searches PATH and calls execv.
 * This function never returns on success; exits on failure.
 */
void exec_single(struct command_t *command) {
  // Input redirection: < file  → redirect stdin to file
  if (command->redirects[0]) {
    int fd = open(command->redirects[0], O_RDONLY);
    if (fd < 0) { perror("open"); exit(1); }
    dup2(fd, STDIN_FILENO);  // replace stdin with file
    close(fd);
  }

  // Output redirection: > file  → redirect stdout to file (truncate)
  if (command->redirects[1]) {
    int fd = open(command->redirects[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); exit(1); }
    dup2(fd, STDOUT_FILENO); // replace stdout with file
    close(fd);
  }

  // Append redirection: >> file → redirect stdout to file (append)
  if (command->redirects[2]) {
    int fd = open(command->redirects[2], O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) { perror("open"); exit(1); }
    dup2(fd, STDOUT_FILENO); // replace stdout with file (append mode)
    close(fd);
  }

  // Part III (b): built-in chatroom command
  if (strcmp(command->name, "chatroom") == 0) {
    run_chatroom(command);
    exit(0); // run_chatroom exits itself, but just in case
  }

  // Part III (a): built-in cut command — handle before external PATH search
  if (strcmp(command->name, "cut") == 0) {
    run_cut(command);
    exit(0);
  }

  // PATH search and execv (same as Part I)
  char *path_env = getenv("PATH");
  if (path_env != NULL) {
    char path_copy[4096];
    strncpy(path_copy, path_env, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *dir = strtok(path_copy, ":");
    while (dir != NULL) {
      char full_path[4096];
      snprintf(full_path, sizeof(full_path), "%s/%s", dir, command->name);
      execv(full_path, command->args); // never returns on success
      dir = strtok(NULL, ":");
    }
  }

  printf("-%s: %s: command not found\n", sysname, command->name);
  exit(127);
}

int process_command(struct command_t *command) {
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[1]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }

  // Part II: Piping
  // If command->next is set, we have a pipeline (e.g. ls | grep foo | wc).
  // We create N-1 pipes for N commands, fork a child for each, and wire
  // their stdin/stdout together using dup2 before calling exec_single.
  if (command->next != NULL) {
    // Count how many commands are in the pipeline
    int count = 0;
    struct command_t *c = command;
    while (c) { count++; c = c->next; }

    // Create count-1 pipes (each pipe connects two adjacent commands)
    int pipes[16][2]; // supports up to 17 piped commands
    for (int i = 0; i < count - 1; i++) {
      if (pipe(pipes[i]) < 0) { perror("pipe"); return UNKNOWN; }
    }

    // Fork one child per command
    c = command;
    for (int i = 0; i < count; i++, c = c->next) {
      pid_t pid = fork();
      if (pid == 0) { // child
        // Read from previous pipe if not the first command
        if (i > 0) {
          dup2(pipes[i - 1][0], STDIN_FILENO);
        }
        // Write to next pipe if not the last command
        if (i < count - 1) {
          dup2(pipes[i][1], STDOUT_FILENO);
        }
        // Close all pipe ends — child only needs the ones already dup2'd
        for (int j = 0; j < count - 1; j++) {
          close(pipes[j][0]);
          close(pipes[j][1]);
        }
        exec_single(c); // handle redirections and exec
      }
    }

    // Parent closes all pipe ends so children can detect EOF
    for (int i = 0; i < count - 1; i++) {
      close(pipes[i][0]);
      close(pipes[i][1]);
    }

    // Wait for all children in the pipeline
    for (int i = 0; i < count; i++) wait(NULL);
    return SUCCESS;
  }

  // No piping — single command execution
  pid_t pid = fork();
  if (pid == 0) { // child
    exec_single(command); // handles redirections + PATH search + execv
  } else {
    // Part I: Background execution
    if (command->background) {
      printf("[bg] PID %d running in background\n", pid);
      // do NOT wait — child runs independently
    } else {
      wait(NULL); // foreground: block until child finishes
    }
    return SUCCESS;
  }
}

int main() {
  while (1) {
    struct command_t *command =
        (struct command_t *)malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}
