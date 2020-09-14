#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define PROMPT "\033[0;34mshell352>\033[0;32m "
#define MAX_LINE 80
#define MAX_ARGS 128

enum builtin_t { NONE, QUIT, CD, KILL };
enum redirect_t { NO, IN, OUT };

struct command {
  int argc;
  char *argv[MAX_ARGS];
  enum builtin_t builtin;
  enum redirect_t redirect;
  char *file;
  char *cmd;
  int bg;
};

struct background {
  pid_t pid;
  struct command *cmd;
  int code;
};

int bg_cmd_count = 0;
struct background bg_cmds[40];

void check_bg_cmds() {
  int exit_code;
  int i;
  int cur_count = 0;

  for (i = 0; i < bg_cmd_count; i++) {
    exit_code = getpgid(bg_cmds[i].pid);
    bg_cmds[i].code = exit_code;

    printf("[%d] Done %s\n", i + 1, bg_cmds[i].cmd->argv[0]);
  }

  for (i = 0; i < bg_cmd_count; i++) {
    if (bg_cmds[i].code != -1 && cur_count != i) {
      bg_cmds[cur_count] = bg_cmds[i];
      cur_count++;
    }
  }

  bg_cmd_count = cur_count;
}

void insert_bg_cmd(pid_t pid, struct command *cmd) {
  struct background bg_cmd;

  printf("[%d] %d\n", bg_cmd_count + 1, pid);

  bg_cmd.pid = pid;
  bg_cmd.cmd = cmd;

  bg_cmds[bg_cmd_count++] = bg_cmd;
}

static int get_length(char *str) {
  int len;

  for (len = 0; str[len] != '\0'; len++)
    ;

  return len;
}

/**
 * Runs the 'cd' command.
 */
int builtin_cd(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "Expected argument to \"cd\"\n");
    return -1;
  } else {
    if (chdir(args[1]) != 0) {
      return -1;
    }
  }

  return 1;
}

/**
 * Runs the 'kill' command.
 */
int builtin_kill(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "No PID provided");
    return -1;
  } else {
    kill(atoi(args[1]), SIGKILL);
  }

  return 1;
}

/**
 * Checks cmd->argv[0] to see if it is a builtin command.
 */
enum builtin_t parse_builtin(struct command *cmd) {
  if (strcmp(cmd->argv[0], "cd") == 0) {
    return (enum builtin_t)CD;
  } else if (strcmp(cmd->argv[0], "exit") == 0) {
    return (enum builtin_t)QUIT;
  } else if (strcmp(cmd->argv[0], "kill") == 0 ||
             strcmp(cmd->argv[0], "KILL") == 0) {
    return (enum builtin_t)KILL;
  } else {
    return (enum builtin_t)NONE;
  }
}

/**
 * Parses the string by breaking it up into words and determines whether the
 * command is builtin or not. This function loops through and when a delimiter
 * is hit, the word gets added to cmd->argv. A function is then called that
 * determines if the command is builtin. This function returns whether the
 * parsed command should run in the background. If it should, then 1 is
 * returned.
 */
void parse_strtok(struct command *cmd) {
  const char delims[10] = " \t\r\n";
  char *ptr;
  int length;

  cmd->argc = 0;
  cmd->redirect = (enum redirect_t)NO;
  cmd->file = "";
  cmd->bg = 0;

  ptr = strtok(cmd->cmd, delims);

  while (ptr != NULL) {
    length = get_length(ptr);

    if (cmd->redirect != (enum redirect_t)NO) {
      cmd->file = ptr;
      break;
    } else {
      cmd->argv[cmd->argc++] = ptr;
    }

    if (length == 1 && ptr[0] == '>') {
      cmd->argc--;
      cmd->redirect = (enum redirect_t)OUT;
    } else if (length == 1 && ptr[0] == '<') {
      cmd->argc--;
      cmd->redirect = (enum redirect_t)IN;
    }

    if (cmd->argc >= MAX_ARGS - 1) {
      break;
    }

    ptr = strtok(NULL, delims);
  }

  cmd->argv[cmd->argc] = NULL;

  if (cmd->argc == 0) {
    return;
  }

  // Checks if the command is a builtin
  cmd->builtin = parse_builtin(cmd);

  // Checks if the command should run the background
  if ((cmd->bg = (*cmd->argv[cmd->argc - 1] == '&')) != 0)
    cmd->argv[--cmd->argc] = NULL;
}

/**
 * This function redirects the input or output of a command. This function first
 * checks if the file exits for a redirect out. If it does not, it will create
 * the file. The redirection is done using dup2.
 */
int redirect(struct command *cmd) {
  int fd = -2;
  FILE *file;

  // Check if there is a redirect in or out
  if (cmd->redirect == (enum redirect_t)OUT) {
    // Creates the file if it does not exist
    if (access(cmd->file, F_OK) == -1) {
      file = fopen(cmd->file, "w");
      if (file == NULL) {
        return -1;
      }

      fclose(file);
    }

    // Attempts to open the file and returns the fd
    if ((fd = open(cmd->file, O_WRONLY)) != -1) {
      dup2(fd, STDOUT_FILENO);
    }
  } else if (cmd->redirect == (enum redirect_t)IN) {
    // Attempts to open the file and returns the fd
    if ((fd = open(cmd->file, O_RDONLY)) != -1) {
      dup2(fd, STDIN_FILENO);
    }
  }

  return fd;
}

/**
 * Runs the given system command by first forking and then waiting for the child
 * process to finish if the command is supposed to run in the foreground.
 */
void run_system_command(struct command *cmd) {
  pid_t child_pid;
  int fd;

  // Forks the current process and sees if it is successful
  if ((child_pid = fork()) < 0) {
    fprintf(stderr, "fork() error");
  } else if (child_pid == 0) { // Runs the command because the PID indicates it
                               // is the child process
    fd = redirect(
        cmd); // Calls redirect helper which redirects input/output if necessary

    if (fd == -1) {
      fprintf(stderr, "Error opening file '%s'", cmd->file);
      exit(0);
    }

    if (execvp(cmd->argv[0], cmd->argv) < 0) {
      fprintf(stderr, "Command not found: %s\n", cmd->argv[0]);
      exit(0);
    }

    // Close file if one was opened.
    if (fd >= 0) {
      close(fd);
    }

  } else {
    if (cmd->bg) {
      insert_bg_cmd(child_pid, cmd);
    } else {
      wait(&child_pid);
    }
    check_bg_cmds();
  }
}

/**
 * Runs the given built in command. The return value of this function is whether
 * the shell should continue. If the shell should exit, 0 is returned from this
 * function.
 */
int run_builtin_command(struct command *cmd) {
  int status = 1;
  pid_t child_pid;

  // Returns 0 if the shell should quit.
  if (cmd->builtin == (enum builtin_t)QUIT) {
    return 0;
  }

  // Forks the current process and sees if it is successful
  if ((child_pid = fork()) < 0) {
    fprintf(stderr, "fork() error");
  } else if (child_pid == 0) { // Runs the command because the PID indicates it
                               // is the child process
    switch (cmd->builtin) {
    case CD:
      status = builtin_cd(cmd->argv);
      break;
    case KILL:
      status = builtin_kill(cmd->argv);
      break;
    default:
      fprintf(stderr, "%s does not exist\n", cmd->argv[0]);
      break;
    }

    if (status < 0) {
      fprintf(stderr, "Error running builtin: %s\n", cmd->argv[0]);
      exit(0);
    }
  } else {
    if (cmd->bg) {
      insert_bg_cmd(child_pid, cmd);
    } else {
      wait(&child_pid);
    }
    check_bg_cmds();
  }

  return status;
}

/**
 * Evaluates the given string. It first parses the command. If the parse command
 * returns -1, there was an error so nothing happens. If cmd.argv[0] == NULL,
 * the user did not pass in a command so nothing happens. If the command is not
 * a builtin, it is passed off to the run_system_command. Otherwise it is passed
 * off to run_builtin_command.
 */
int eval(char *cmdline) {
  struct command cmd;
  int ret;

  cmd.cmd = malloc(strlen(cmdline) * 8);
  strcpy(cmd.cmd, cmdline);

  // Parses the command
  parse_strtok(&cmd);

  // If bg is -1 or the command is NULL, we return to get the next command
  if (cmd.bg == -1 || cmd.argv[0] == NULL) {
    ret = 1;
  } else if (cmd.builtin ==
             (enum builtin_t)NONE) { // Runs the system command and returns

    run_system_command(&cmd);
    ret = 1;
  } else { // Runs builtin command and returns it's return value
    ret = run_builtin_command(&cmd);
  }

  free(cmd.cmd);

  return ret;
}

int main(void) {
  char cmdline[MAX_LINE];
  int should_run = 1;
  char *fgets_r;
  int ferror_r;

  while (should_run) {
    printf("%s", PROMPT);
    fflush(stdout);

    // Gets input from stdin
    fgets_r = fgets(cmdline, MAX_LINE, stdin);
    ferror_r = ferror(stdin);

    if (fgets_r == NULL && ferror_r) {
      fprintf(stderr, "fgets error");
    }

    if (feof(stdin)) {
      printf("\n");
      exit(0);
    }

    cmdline[strlen(cmdline) - 1] = '\0';

    should_run = eval(cmdline);
  }

  return 0;
}