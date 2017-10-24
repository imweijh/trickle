#define _BSD_SOURCE /* deprecated */
#define _DEFAULT_SOURCE

#include <sys/param.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <time.h>

#if defined(__APPLE__)
# include <util.h>
#elif defined(BSD)
# include <libutil.h>
#else
# include <pty.h>
#endif

#define USAGE "usage: trtty [-b BITRATE] COMMAND [ARG ...]"

static int fdchild;
static struct termios termios_orig;

static void
onsigwinch(int sig)
{
	struct winsize winsize;

	if (!fdchild)
		return;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsize) == -1)
		return;

	ioctl(fdchild, TIOCSWINSZ, &winsize);
}

static void
restoreterm(void)
{
	tcsetattr(STDIN_FILENO, TCSADRAIN, &termios_orig);
}

int
main(int argc, char **argv)
{
	long rate = 600;
	int i;
	char c, *rest;
	double d;
	struct timespec delay;
	struct pollfd pollfds[2];
	struct winsize winsize;
	struct termios termios;

	while ((i = getopt(argc, argv, "b:")) != -1) {
		switch (i) {
		case 'b':
			d = strtod(optarg, &rest);
			if (!*rest)
				;
			else if (!strcmp(rest, "k"))
				d *= 1000;
			else {
				fputs("bad -b format\n", stderr);
				return 1;
			}
			rate = (long)d;	
			break;
		default:
			fputs(USAGE "\n", stderr);
			return 1;
		}
	}

	if (rate < 50 || rate > 57600) {
		fputs("bad -b, must be 50-57600\n", stderr);
		return 1;
	}

	if (!argv[optind]) {
		fputs(USAGE "\n", stderr);
		return 1;
	}

	if (!isatty(STDIN_FILENO) || !isatty(STDIN_FILENO)) {
		fputs("not a tty\n", stderr);
		return 1;
	}

	pollfds[0].fd = STDIN_FILENO;
	pollfds[0].events = POLLIN;

	delay.tv_sec = 0;
	delay.tv_nsec = 8e9 / rate;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsize) == -1) {
		perror("TIOCGWINSZ ioctl");
		return 1;
	}

	signal(SIGWINCH, onsigwinch);

	switch (forkpty(&fdchild, NULL, NULL, &winsize)) {
	case -1:
		perror("forkpty");
		return 1;
	case 0:
		execvp(argv[optind], argv+optind);
		perror("execvp");
		return 1;
	}

	if (tcgetattr(STDIN_FILENO, &termios) == -1) {
		perror("tcgetattr");
		return -1;
	}

	termios_orig = termios;
	atexit(restoreterm);
	cfmakeraw(&termios);

	if (tcsetattr(STDIN_FILENO, TCSADRAIN, &termios) == -1) {
		perror("tcsetattr");
		return -1;
	}

	pollfds[1].fd = fdchild;
	pollfds[1].events = POLLIN;

	while (1) {
		poll(pollfds, 2, 0);

		if (pollfds[0].revents) {
			if (read(STDIN_FILENO, &c, 1) != 1)
				break;
			if (write(fdchild, &c, 1) != 1)
				break;
		}

		if (pollfds[1].revents) {
			if (read(fdchild, &c, 1) != 1)
				break;
			if (write(STDOUT_FILENO, &c, 1) != 1)
				break;
		}

		nanosleep(&delay, NULL);
	}

	close(fdchild);

	return 0;
}