/*--------------------------------------------------------------------
 *    The MB-system:  termios.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * MSVC substitute for POSIX <termios.h>, on the TRN include path for WIN32
 * only. Used by the serial data sources: mbtrnutils/mbtrnpp.c,
 * mbtrn/utils/emserpub.c and mbtrnframe/mserial.c.
 *
 * This is a real implementation over the Win32 DCB/COMMTIMEOUTS API rather
 * than a set of no-ops: a stub that quietly accepted the settings would leave
 * a serial port running at whatever baud rate it happened to be on, which is
 * far worse than failing. The descriptor is a CRT file descriptor (from
 * open()/_open on a COM device), converted to a HANDLE with _get_osfhandle.
 *
 * What is covered is the subset the TRN sources use: baud rate, character
 * size, stop bits, parity, hardware and software flow control, raw mode, and
 * the VMIN/VTIME read timeouts. Line-discipline flags that Windows has no
 * equivalent for (ISIG, IEXTEN, ECHONL, ...) are accepted and stored so that
 * a get/set round trip preserves them, but have no effect.
 */

#ifndef MBTRN_W32_TERMIOS_H
#define MBTRN_W32_TERMIOS_H

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <io.h>
#include <errno.h>
#include <string.h>

/* c_cflag */
#define CSIZE   0x00000030
#define CS5     0x00000000
#define CS6     0x00000010
#define CS7     0x00000020
#define CS8     0x00000030
#define CSTOPB  0x00000040
#define CREAD   0x00000080
#define PARENB  0x00000100
#define PARODD  0x00000200
#define HUPCL   0x00000400
#define CLOCAL  0x00000800
#define CRTSCTS 0x80000000

/* c_iflag */
#define IGNBRK  0x00000001
#define BRKINT  0x00000002
#define IGNPAR  0x00000004
#define PARMRK  0x00000008
#define INPCK   0x00000010
#define ISTRIP  0x00000020
#define INLCR   0x00000040
#define IGNCR   0x00000080
#define ICRNL   0x00000100
#define IXON    0x00000400
#define IXANY   0x00000800
#define IXOFF   0x00001000

/* c_oflag */
#define OPOST   0x00000001
#define ONLCR   0x00000004

/* c_lflag */
#define ISIG    0x00000001
#define ICANON  0x00000002
#define ECHO    0x00000008
#define ECHOE   0x00000010
#define ECHOK   0x00000020
#define ECHONL  0x00000040
#define NOFLSH  0x00000080
#define IEXTEN  0x00008000

/* c_cc indices. The numbering follows Linux so that code copying these
 * around keeps working; only VMIN/VTIME have an effect here. */
#define VINTR    0
#define VQUIT    1
#define VERASE   2
#define VKILL    3
#define VEOF     4
#define VTIME    5
#define VMIN     6
#define VSTART   8
#define VSTOP    9
#define VSUSP   10
#define VEOL    11
#define NCCS    32

/* tcsetattr() actions. Windows applies immediately in every case. */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* tcflush() queue selectors */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

/* Baud rates. Unlike POSIX these are the rates themselves, which keeps the
 * mapping to the DCB trivial and still compares equal the way the sources
 * use them. */
#define B0      0
#define B50     50
#define B75     75
#define B110    110
#define B134    134
#define B150    150
#define B200    200
#define B300    300
#define B600    600
#define B1200   1200
#define B1800   1800
#define B2400   2400
#define B4800   4800
#define B9600   9600
#define B19200  19200
#define B38400  38400
#define B57600  57600
#define B115200 115200
#define B230400 230400

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t     c_cc[NCCS];
	speed_t  c_ispeed;
	speed_t  c_ospeed;
};

/* Modem status lines. emserpub reads these with ioctl(TIOCMGET); on Windows
 * they come from GetCommModemStatus, wrapped as ioctl() below. */
#define TIOCMGET   0x5415
#define TIOCMSET   0x5418
#define TIOCM_LE   0x001
#define TIOCM_DTR  0x002
#define TIOCM_RTS  0x004
#define TIOCM_CTS  0x020
#define TIOCM_CAR  0x040
#define TIOCM_CD   TIOCM_CAR
#define TIOCM_RNG  0x080
#define TIOCM_RI   TIOCM_RNG
#define TIOCM_DSR  0x100

#ifdef __cplusplus
extern "C" {
#endif

static __inline HANDLE mbtrn_w32_tty_handle(int fd) {
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	return (h == INVALID_HANDLE_VALUE) ? NULL : h;
}

static __inline int cfsetispeed(struct termios *t, speed_t speed) {
	if (t == NULL) return -1;
	t->c_ispeed = speed;
	return 0;
}

static __inline int cfsetospeed(struct termios *t, speed_t speed) {
	if (t == NULL) return -1;
	t->c_ospeed = speed;
	return 0;
}

static __inline speed_t cfgetispeed(const struct termios *t) { return t ? t->c_ispeed : 0; }
static __inline speed_t cfgetospeed(const struct termios *t) { return t ? t->c_ospeed : 0; }

/* Raw mode: no line discipline, no echo, no input/output translation. */
static __inline void cfmakeraw(struct termios *t) {
	if (t == NULL) return;
	t->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	t->c_oflag &= ~OPOST;
	t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	t->c_cflag &= ~(CSIZE | PARENB);
	t->c_cflag |= CS8;
	t->c_cc[VMIN] = 1;
	t->c_cc[VTIME] = 0;
}

static __inline int tcgetattr(int fd, struct termios *t) {
	DCB dcb;
	COMMTIMEOUTS timeouts;
	HANDLE h = mbtrn_w32_tty_handle(fd);

	if (h == NULL || t == NULL) {
		errno = EBADF;
		return -1;
	}
	memset(&dcb, 0, sizeof(dcb));
	dcb.DCBlength = sizeof(dcb);
	if (!GetCommState(h, &dcb)) {
		errno = ENOTTY;
		return -1;
	}

	memset(t, 0, sizeof(*t));
	t->c_ispeed = t->c_ospeed = (speed_t)dcb.BaudRate;
	t->c_cflag = CREAD | CLOCAL;

	switch (dcb.ByteSize) {
		case 5:  t->c_cflag |= CS5; break;
		case 6:  t->c_cflag |= CS6; break;
		case 7:  t->c_cflag |= CS7; break;
		default: t->c_cflag |= CS8; break;
	}
	if (dcb.StopBits == TWOSTOPBITS) t->c_cflag |= CSTOPB;
	if (dcb.fParity && dcb.Parity != NOPARITY) {
		t->c_cflag |= PARENB;
		if (dcb.Parity == ODDPARITY) t->c_cflag |= PARODD;
	}
	if (dcb.fOutxCtsFlow || dcb.fRtsControl == RTS_CONTROL_HANDSHAKE) t->c_cflag |= CRTSCTS;
	if (dcb.fOutX) t->c_iflag |= IXON;
	if (dcb.fInX)  t->c_iflag |= IXOFF;

	if (GetCommTimeouts(h, &timeouts)) {
		t->c_cc[VTIME] = (cc_t)(timeouts.ReadTotalTimeoutConstant / 100);
		t->c_cc[VMIN] = (timeouts.ReadIntervalTimeout == MAXDWORD) ? 0 : 1;
	}
	return 0;
}

static __inline int tcsetattr(int fd, int action, const struct termios *t) {
	DCB dcb;
	COMMTIMEOUTS timeouts;
	HANDLE h = mbtrn_w32_tty_handle(fd);

	(void)action;   /* Windows always applies the settings immediately */
	if (h == NULL || t == NULL) {
		errno = EBADF;
		return -1;
	}
	memset(&dcb, 0, sizeof(dcb));
	dcb.DCBlength = sizeof(dcb);
	if (!GetCommState(h, &dcb)) {
		errno = ENOTTY;
		return -1;
	}

	if (t->c_ospeed != 0) dcb.BaudRate = (DWORD)t->c_ospeed;
	else if (t->c_ispeed != 0) dcb.BaudRate = (DWORD)t->c_ispeed;

	switch (t->c_cflag & CSIZE) {
		case CS5: dcb.ByteSize = 5; break;
		case CS6: dcb.ByteSize = 6; break;
		case CS7: dcb.ByteSize = 7; break;
		default:  dcb.ByteSize = 8; break;
	}
	dcb.StopBits = (t->c_cflag & CSTOPB) ? TWOSTOPBITS : ONESTOPBIT;

	if (t->c_cflag & PARENB) {
		dcb.fParity = TRUE;
		dcb.Parity = (t->c_cflag & PARODD) ? ODDPARITY : EVENPARITY;
	}
	else {
		dcb.fParity = FALSE;
		dcb.Parity = NOPARITY;
	}

	if (t->c_cflag & CRTSCTS) {
		dcb.fOutxCtsFlow = TRUE;
		dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
	}
	else {
		dcb.fOutxCtsFlow = FALSE;
		dcb.fRtsControl = RTS_CONTROL_ENABLE;
	}
	dcb.fOutX = (t->c_iflag & IXON) ? TRUE : FALSE;
	dcb.fInX = (t->c_iflag & IXOFF) ? TRUE : FALSE;
	dcb.fBinary = TRUE;              /* Windows supports no other mode */
	dcb.fDtrControl = DTR_CONTROL_ENABLE;
	dcb.fAbortOnError = FALSE;

	if (!SetCommState(h, &dcb)) {
		errno = EINVAL;
		return -1;
	}

	/* VMIN/VTIME: VMIN 0 means "return whatever is available immediately",
	 * which is the totally-non-blocking timeout pattern on Windows. VTIME is
	 * in tenths of a second. */
	memset(&timeouts, 0, sizeof(timeouts));
	if (t->c_cc[VMIN] == 0) {
		timeouts.ReadIntervalTimeout = MAXDWORD;
		timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
		timeouts.ReadTotalTimeoutConstant = (DWORD)t->c_cc[VTIME] * 100;
	}
	else {
		timeouts.ReadIntervalTimeout = (t->c_cc[VTIME] != 0) ? (DWORD)t->c_cc[VTIME] * 100 : 0;
		timeouts.ReadTotalTimeoutConstant = 0;
		timeouts.ReadTotalTimeoutMultiplier = 0;
	}
	SetCommTimeouts(h, &timeouts);
	return 0;
}

static __inline int tcflush(int fd, int queue) {
	DWORD flags = 0;
	HANDLE h = mbtrn_w32_tty_handle(fd);

	if (h == NULL) {
		errno = EBADF;
		return -1;
	}
	if (queue == TCIFLUSH || queue == TCIOFLUSH) flags |= PURGE_RXCLEAR;
	if (queue == TCOFLUSH || queue == TCIOFLUSH) flags |= PURGE_TXCLEAR;
	return PurgeComm(h, flags) ? 0 : -1;
}

/* ioctl() for the modem-status requests emserpub makes. Anything else is
 * rejected rather than silently succeeding. */
static __inline int ioctl(int fd, unsigned long request, void *arg) {
	HANDLE h = mbtrn_w32_tty_handle(fd);
	DWORD status = 0;
	int *out = (int *)arg;

	if (h == NULL || arg == NULL) {
		errno = EBADF;
		return -1;
	}
	if (request == TIOCMGET) {
		if (!GetCommModemStatus(h, &status)) {
			errno = EINVAL;
			return -1;
		}
		*out = 0;
		if (status & MS_CTS_ON)  *out |= TIOCM_CTS;
		if (status & MS_DSR_ON)  *out |= TIOCM_DSR;
		if (status & MS_RING_ON) *out |= TIOCM_RNG;
		if (status & MS_RLSD_ON) *out |= TIOCM_CAR;
		return 0;
	}
	if (request == TIOCMSET) {
		EscapeCommFunction(h, (*out & TIOCM_DTR) ? SETDTR : CLRDTR);
		EscapeCommFunction(h, (*out & TIOCM_RTS) ? SETRTS : CLRRTS);
		return 0;
	}
	errno = ENOSYS;
	return -1;
}

static __inline int tcdrain(int fd) {
	HANDLE h = mbtrn_w32_tty_handle(fd);

	if (h == NULL) {
		errno = EBADF;
		return -1;
	}
	return FlushFileBuffers(h) ? 0 : -1;
}

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */

#endif /* MBTRN_W32_TERMIOS_H */
