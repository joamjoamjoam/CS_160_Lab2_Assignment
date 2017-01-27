/*
 * tsh - A tiny shell program with job control
 *
 * Trent Callan
 * SID: 861117907
 *
 * 2017 Copyright Trent Callan
 * This is my unique code written fully by the named above and is not to be used in any other way.
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */
#define MYFGGROUPID   7907

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */


/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

// my debug verbose function
inline int doNothing(char const* format,...){ return 0; };

int (*debugLog)(char const *,...) = &doNothing;



/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
int nextPGID = 100;         // next process group id to allocate
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/*********************************
 * My Changes To Given Functions
 * int parseArgc(const char *cmdline, char **argv);
 * i reused pasreLine and chaged it to return argc
 * int builtin_cmd(char **argv, int argc);
 * i modified this toallow the passing of argc to built in functions
 * void do_bgfg(char **argv,argc);
 * i modified this toallow the passing of argc to bg and fg
 
 */


/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv, int argc);
void do_bgfg(char **argv, int argc);
void waitfg(pid_t pid);
int getNextPGID();

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

// my helper functions
int parseArgc(const char *cmdline, char **argv);
int hasDisallowedChars(char* tmp);

/* Here are helper routines that we've provided for you */
int parseLine(const char *cmdline, char **argv);
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine
 */
int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */
    
    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);
    
    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
            case 'h':             /* print help message */
                usage();
                break;
            case 'v':             /* emit additional diagnostic info */
                verbose = 1;
                debugLog = &printf;
                break;
            case 'p':             /* don't print a prompt */
                emit_prompt = 0;  /* handy for automatic testing */
                break;
            default:
                usage();
        }
    }
    
    /* Install the signal handlers */
    
    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */
    
    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);
    
    /* Initialize the job list */
    initjobs(jobs);
    
    /* Execute the shell's read/eval loop */
    while (1) {
        
        /* Read command line */
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        // handle empty command line send
        if (feof(stdin)) { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }
        
        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    }
    // removing control never reaches warning
    //exit(0); /* control never reaches here */
}

#pragma mark User Implemented Functions

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 */
void eval(char *cmdLine)
{
    if (!strcmp("\n", cmdLine)) {
        debugLog("Command Line is Empty\n");
        return;
    }
    assert(strcmp("\n", cmdLine) && "commandLine must not be empty");
    
    debugLog("cmdLine = %s",cmdLine);
    
    char* argv[MAXARGS];
    char* ignoreArgv[MAXARGS];
    int argc = parseArgc(cmdLine, ignoreArgv);
    char commandName[MAXLINE];
    int childPid= 0;
    int runInBackground = parseLine(cmdLine,argv);
    strcpy(commandName,argv[0]);
    // print parsed command to stdout seperated by | ex ls | -v | ./example
    debugLog("ParsedCommandName = %s\n", commandName);
    debugLog("Parsed Argument Count = %d\n", argc);
    
    if(strcmp("",commandName)){ // strcmp return 0 if equal
        // check for built in commands
        if(builtin_cmd(argv, argc)){
            //printf("%s ran by builtin_cmd not eval\n",commandName);
        }
        else{
            // is process running in the background?
            
            // run process in background and dont hold shell
            
            if((childPid = fork()) == 0){
                // child process
                char* iterator = commandName;
                int absPath = 0;
                // install signal handlers to child process.
                Signal(SIGINT,  sigint_handler);
                Signal(SIGTSTP, sigtstp_handler);
                Signal(SIGCHLD, sigchld_handler);
                
                // wait for parent pgid change
                
                while (*iterator)
                {
                    if (strchr("/", *iterator))
                    {
                        // command name contains a slash
                        absPath = 1;
                    }
                    
                    iterator++;
                }
                
                if (!absPath) {
                    sprintf(commandName, "/bin/%s",argv[0]);
                }
                
                debugLog("Child has pgid %d\n", getpgrp());
                fflush(stdout);
                //sleep (5); // wait for pgid change
                execve(commandName, argv, environ);
                
                // this only runs if execve fails
                printf("%s: Command Not Found\n",commandName);
                exit(1);
                
            }
            else{
                // parent process
                // first give the child process a new group id
                debugLog("Setting pid %d to pgid %d from old pgid %d\n",childPid,childPid,getpgid(childPid));
                setpgid(childPid, 0);
                if (runInBackground) {
                    addjob(jobs, childPid, BG, cmdLine);
                    printf("[%d] (%d) %s", pid2jid(childPid), childPid, cmdLine);
                }
                else{
                    addjob(jobs, childPid, FG, cmdLine);
                    waitfg(childPid);
                }
            }
        }
    }
    
    return;
}
/*
 * getNextPGID - Gets the next unique process group id for a a child process
 *
 */
int getNextPGID(){
    int tmp = nextPGID;
    nextPGID = nextPGID + 1;
    return tmp;
}

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 */
int parseLine(const char *cmdline, char **argv)
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */
    
    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;
    
    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    }
    else {
        delim = strchr(buf, ' ');
    }
    
    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;
        
        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        }
        else {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;
    
    if (argc == 0){  /* ignore blank line */
        return 1;
    }
    
    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
        argv[--argc] = NULL;
    }
    return bg;
}

/*
 * parseArgc - Parse the command line and build the argc.
 *
 * re running parseline but instead returning argc
 */
int parseArgc(const char *cmdline, char **argv)
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */
    
    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;
    
    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    }
    else {
        delim = strchr(buf, ' ');
    }
    
    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;
        
        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        }
        else {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;
    
    if (argc == 0){  /* ignore blank line */
        return 0;
    }
    
    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
        argv[--argc] = NULL;
    }
    return argc;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 */
int builtin_cmd(char **argv, int argc)
{
    // if it is a bulit in command run it here and return 1
    // if not retunr 0 to tell eval that it must run it there
    int ranSomething = 0;
    if(!strcmp("quit",argv[0])){
        debugLog(("Running Quit in builtin_cmd\n"));
        exit(0);
        
    }
    else if(!strcmp("bg",argv[0])){
        debugLog("Ran bg\n");
        do_bgfg(argv,argc);
        ranSomething = 1;
    }
    else if(!strcmp("fg",argv[0])){
        debugLog("ran fg\n");
        do_bgfg(argv,argc);
        ranSomething = 1;
    }
    else if(!strcmp("jobs",argv[0])){
        listjobs(jobs);
        ranSomething = 1;
    }
    
    return ranSomething;     /* not a builtin command */
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 * #define FG 1
 * #define BG 2
 * #define ST 3
 */
void do_bgfg(char **argv, int argc)
{
    char commandName[MAXLINE];
    strcpy(commandName, argv[0]);
    
    if (argc >= 2 && !hasDisallowedChars(argv[0])) {
        debugLog("Correct Argments for %s found.\n", commandName);
    }
    else{
        printf("%s command requires PID or %%jobid argument\n", commandName);
        return;
    }
    
    
    // have to clean up jid to find first bg %2 to bg 2 if % then its a job id if no % hen its a pid
    int jidToStateChange;
    struct job_t* jobToChange;
    pid_t pidToStateChange;
    
    
    if (argv[1][0] == '%') {
        // is job
        int jidToSearchFor =  atoi(argv[1] + 1);
        jobToChange = getjobjid(jobs, jidToSearchFor);
        if (!jobToChange) {
            printf("%s: %s: no such job\n", commandName, argv[1]);
            return;
        }
    }
    else{
        // is process
        pid_t pidToSearchFor =  atoi(argv[1]);
        
        jobToChange = getjobpid(jobs, pidToSearchFor);
        
        // check if process exists in jobs list
        if (!jobToChange) {
            printf("%s: %s: no such process\n", commandName, argv[1]);
            return;
        }
    }
    assert(jobToChange && "There must be a job to fg/bg");
    pidToStateChange = jobToChange->pid;
    jidToStateChange = jobToChange->jid;
    
    debugLog("%sing (%d) from previous state %d\n",commandName,jidToStateChange,jobToChange->state);
    
    if (!strcmp("fg", commandName)) {
        // foreground process
        
        if (jobToChange->state == ST) {
            // change fg job to BG state to allow new process to have FG state
            jobToChange->state = FG;
            int test = fgpid(jobs);
            assert((test > 0 ) && "There can only be one FG job");
            debugLog("[%d] (%d) %s",pid2jid(pidToStateChange), pidToStateChange, jobToChange->cmdline);
            kill(pidToStateChange, SIGCONT); // restart process
            
            waitfg(pidToStateChange); // this may not be right
            
            // new process terminated so we have to fg fg process
            // then fg it
        }
        else if(jobToChange->state == BG){
            // foreground process first
            jobToChange->state = FG;
            int test = fgpid(jobs);
            assert((test > 0) && "There can only be one FG job");
            debugLog("[%d] (%d) %s",pid2jid(pidToStateChange), pidToStateChange, jobToChange->cmdline);
            waitfg(pidToStateChange);
            
            // new process terminated so we have to fg fg process
        }
        else{
            // state = FG do nothing
        }
    }
    else{
        // background process
        if (jobToChange->state == ST) {
            printf("[%d] (%d) %s",pid2jid(pidToStateChange), pidToStateChange, jobToChange->cmdline);
            kill(pidToStateChange, SIGCONT);
        }
        else{
            // state = BG or FG do nothing
        }
        jobToChange->state = BG;
    }
    
    
    return;
}

/*
 * testForChars(char* tmp) test if there are any of the disallowed chars in tmp
 *
 */
int hasDisallowedChars(char* tmp){
    const char *disallowedChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char *iterator = tmp;
    while (*iterator)
    {
        if (strchr(disallowedChars, *iterator))
            return 1;
        
        iterator++;
    }
    return 0;
}

/*
 * waitfg - Block until process pid is no longer the foreground process based on job list
 */
void waitfg(pid_t pid){
    int returnedStatus;
    int signalingPID = waitpid(pid, &returnedStatus, WUNTRACED);
    fflush(stdout);
    
    if (signalingPID == -1) {
        debugLog("waitpid returned error in waitfg");
        return;
    }
    else if (WIFEXITED(returnedStatus)){
        // process terminated by exit clean up child by killig it
        debugLog("FG process %d terminated with exit status %d\n", signalingPID, WEXITSTATUS(returnedStatus));
        fflush(stdout);
        deletejob(jobs, signalingPID);
        kill(signalingPID, SIGTERM);
    }
    else if(WIFSIGNALED(returnedStatus)){
        // terminated by a signal
        int signal = WTERMSIG(returnedStatus);
        printf("Job [%d] (%d) terminated by signal %d\n",pid2jid(signalingPID),signalingPID,signal);
        fflush(stdout);
        deletejob(jobs, signalingPID);
        kill(signalingPID, SIGTERM);
    }
    else if (WIFSTOPPED(returnedStatus)){
        printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(signalingPID),signalingPID ,WSTOPSIG(returnedStatus));
        fflush(stdout);
        struct job_t* tmp = getjobpid(jobs, signalingPID);
        tmp->state = ST;
    }
    else{
        debugLog("FG process %d terminated wierdly\n", signalingPID);
        fflush(stdout);
        kill(signalingPID, SIGTERM);
    }
//    while (pid == fgpid(jobs) || fgpid(jobs) != 0) {
//        sleep(.5);
//    }
    return;
}

/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
void sigchld_handler(int sig)
{
    if (sig == SIGCHLD){
        int returnedStatus;
        int signalingPID = 0;
        while ((signalingPID = waitpid(0, &returnedStatus, WNOHANG)) > 0) {
            debugLog("SIGCHLD recieved from pid: %d\n", signalingPID);
            fflush(stdout);
            
            if (WIFEXITED(returnedStatus)){
                // process terminated by exit clean up child by killig it
                debugLog("Child %d terminated with exit status %d\n", signalingPID, WEXITSTATUS(returnedStatus));
                fflush(stdout);
                deletejob(jobs, signalingPID);
                kill(signalingPID, SIGTERM);
            }
            else if(WIFSIGNALED(returnedStatus)){
                // terminated by a signal
                int signal = WTERMSIG(returnedStatus);
                printf("Job [%d] (%d) terminated by signal %d\n",pid2jid(signalingPID),signalingPID,signal);
                fflush(stdout);
                deletejob(jobs, signalingPID);
                kill(signalingPID, SIGTERM);
            }
            else if (WIFSTOPPED(returnedStatus)){
                debugLog("Child %d was stopped.\n", signalingPID);
                fflush(stdout);
                struct job_t* tmp = getjobpid(jobs, signalingPID);
                tmp->state = ST;
            }
            else{
                debugLog("Child %d terminated wierdly\n", signalingPID);
                fflush(stdout);
                kill(signalingPID, SIGTERM);
            }
            
        }
        return;
    }
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig)
{
    if (sig == SIGINT){
        printf("\n");
        debugLog("User Pressed ctrl-c\n");
        debugLog("Killing Foreground job\n");
        
        pid_t fgPID = fgpid(jobs);
        debugLog("fgPID = %d\n",fgPID);
        if (fgPID > 0) {
            kill(fgPID,SIGINT);
            debugLog("Forwarded SIGINT to pid: %d\n", fgPID);
        }
        else{
            debugLog("No fg process ignoring SIGINT\n");
            printf("%s",prompt);
        }
        fflush(stdout);
    }
    
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig)
{
    if (sig == SIGTSTP){
        printf("\n");
        debugLog("User Pressed ctrl-z\n");
        debugLog("Stopping Foreground job\n");
        
        pid_t fgPID = fgpid(jobs);
        debugLog("fgPID = %d\n",fgPID);
        if (fgPID > 0) {
            // first bg the fg process then stop it otherwise term never gets control back
            kill(fgPID,SIGTSTP);
            debugLog("Forwarded SIGTSTP to pid: %d\n", fgPID);
            struct job_t* tmp = getjobpid(jobs, fgPID);
            tmp->state = ST;
        }
        else{
            debugLog("No fg process ignoring SIGTSTP\n");
            printf("%s",prompt);
        }
        fflush(stdout);
    }
    return;
}

/*********************
 * End signal handlers
 *********************/
#pragma mark Given Helper Functions
/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;
    
    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs)
{
    int i, max=0;
    
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid > max)
            max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
    int i;
    
    if (pid < 1)
        return 0;
    
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(jobs[i].cmdline, cmdline);
            if(verbose){
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid)
{
    int i;
    
    if (pid < 1)
        return 0;
    
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs)+1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;
    
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG)
            return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;
    
    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid)
{
    int i;
    
    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid)
{
    int i;
    
    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
                case BG:
                    printf("Running ");
                    break;
                case FG:
                    printf("Foreground ");
                    break;
                case ST:
                    printf("Stopped ");
                    break;
                default:
                    printf("listjobs: Internal error: job[%d].state=%d ",
                           i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void)
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler)
{
    struct sigaction action, old_action;
    
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */
    
    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig)
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}



